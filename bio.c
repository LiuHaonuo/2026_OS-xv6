// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
struct spinlock eviction_lock;
#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKET_NUM 13   // 质数，减少哈希冲突

// 每个桶的结构
struct bucket {
  struct spinlock lock;
  struct buf head;     // 双向链表头
};

// 哈希表结构
struct {
  struct bucket buckets[BUCKET_NUM];
  struct buf buf[NBUF];
} bcache;

// 哈希函数
#define HASH(dev, blockno) (((dev) ^ (blockno)) % BUCKET_NUM)

void
binit(void)
{
  struct buf *b;
  int i;

  initlock(&eviction_lock, "bcache_evict");

  // 初始化每个桶的锁和链表
  for (int bi = 0; bi < BUCKET_NUM; bi++) {
    char name[8];
    snprintf(name, sizeof(name), "bcache_%d", bi);
    initlock(&bcache.buckets[bi].lock, name);
    bcache.buckets[bi].head.prev = &bcache.buckets[bi].head;
    bcache.buckets[bi].head.next = &bcache.buckets[bi].head;
  }

  // 初始化所有缓冲区，放入 bucket 0
  for (i = 0; i < NBUF; i++) {
    b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->dev = -1;       // 标记为未使用
    b->blockno = -1;
    b->refcnt = 0;
    b->valid = 0;
    b->timestamp = 0;
    
    // 加入 bucket 0 的链表
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bi = HASH(dev, blockno);
  struct bucket *bucket = &bcache.buckets[bi];

  // 在当前桶中查找
  acquire(&bucket->lock);
  for (b = bucket->head.next; b != &bucket->head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      b->timestamp = ticks;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);

  // ⭐ 使用全局锁序列化 LRU 查找和移动
  acquire(&eviction_lock);
  
  struct buf *lru = 0;
  uint oldest_ts = ~0U;
  int lru_bucket = -1;

  for (int i = 0; i < BUCKET_NUM; i++) {
    acquire(&bcache.buckets[i].lock);
    for (b = bcache.buckets[i].head.next; 
         b != &bcache.buckets[i].head; 
         b = b->next) {
      if (b->refcnt == 0 && b->timestamp < oldest_ts) {
        oldest_ts = b->timestamp;
        lru = b;
        lru_bucket = i;
      }
    }
    release(&bcache.buckets[i].lock);
  }

  if (lru == 0) {
    release(&eviction_lock);
    panic("bget: no buffers");
  }

  // 从源桶移除
  acquire(&bcache.buckets[lru_bucket].lock);
  if (lru->refcnt != 0) {
    release(&bcache.buckets[lru_bucket].lock);
    release(&eviction_lock);
    panic("bget: buffer became referenced");
  }
  lru->prev->next = lru->next;
  lru->next->prev = lru->prev;
  release(&bcache.buckets[lru_bucket].lock);
  
  release(&eviction_lock);

  // 放入目标桶
  acquire(&bucket->lock);
  // 再次检查目标桶中是否有人插入了这个块
  for (b = bucket->head.next; b != &bucket->head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      b->timestamp = ticks;
      release(&bucket->lock);
      // 将 lru 放回原来的桶
      acquire(&bcache.buckets[lru_bucket].lock);
      lru->prev->next = lru->next;
      lru->next->prev = lru->prev;
      release(&bcache.buckets[lru_bucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 使用 lru 缓冲区
  lru->dev = dev;
  lru->blockno = blockno;
  lru->refcnt = 1;
  lru->timestamp = ticks;
  lru->valid = 0;

  lru->next = bucket->head.next;
  lru->prev = &bucket->head;
  bucket->head.next->prev = lru;
  bucket->head.next = lru;

  release(&bucket->lock);

  acquiresleep(&lru->lock);
  return lru;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bi = HASH(b->dev, b->blockno);
  acquire(&bcache.buckets[bi].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks;
  }
  release(&bcache.buckets[bi].lock);
}

void
bpin(struct buf *b)
{
  int bi = HASH(b->dev, b->blockno);
  acquire(&bcache.buckets[bi].lock);
  b->refcnt++;
  release(&bcache.buckets[bi].lock);
}

void
bunpin(struct buf *b)
{
  int bi = HASH(b->dev, b->blockno);
  acquire(&bcache.buckets[bi].lock);
  b->refcnt--;
  release(&bcache.buckets[bi].lock);
}
