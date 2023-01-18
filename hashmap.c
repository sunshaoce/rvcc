// 本文件是对开放寻址哈希表（Open Addressing Hash Table）的一种实现。

#include "rvcc.h"

// 初始哈希表的大小，即哈希表一开始能存储16个键值对
#define INIT_SIZE 16

// 高容量线，超过70%重新进行哈希计算
#define HIGH_WATERMARK 70

// 低容量线，超过后会需要扩充空间
#define LOW_WATERMARK 50

// 表示一个删除掉的哈希键值对
// 插入新元素时，优先选择标记删除的键值对，可以尽量避免增加哈希表的容量
#define TOMBSTONE ((void *)-1)

// 64位FNV-1哈希算法
// 能对字符串进行高效的哈希值计算
static uint64_t fnvHash(char *S, int Len) {
  // FNV初始偏移值（FNV_offset_basis）
  uint64_t Hash = 0xCBF29CE484222325;
  // 遍历字符串中的每个字节进行计算
  for (int I = 0; I < Len; I++) {
    // 哈希值乘以FNV质数（FNV_prime，此处为64位的）
    Hash *= 0x100000001B3;
    // 哈希值与该字节的值进行异或
    Hash ^= (unsigned char)S[I];
  }
  // 返回最后计算出的哈希值
  return Hash;
}

// 通过删除墓碑标记，为新的键值对开辟空间，并有可能拓展桶大小
static void rehash(HashMap *Map) {
  // 计算新哈希表的大小
  // 记录当前哈希表中使用的键值对数量
  int NKeys = 0;
  // 遍历当前哈希表的每个桶
  for (int I = 0; I < Map->Capacity; I++)
    // 如果当前桶中使用的键值对并且未被标记删除
    if (Map->Buckets[I].Key && Map->Buckets[I].Key != TOMBSTONE)
      NKeys++;

  // Cap存储新的容量，初始值为当前哈希表的容量
  int Cap = Map->Capacity;
  // 如果键值对使用数量超过了LOW_WATERMARK，则
  while ((NKeys * 100) / Cap >= LOW_WATERMARK)
    // 将Cap的值乘以2，这样就能翻倍哈希表的容量
    Cap = Cap * 2;
  // 若Cap的值不大于0，则终止程序
  assert(Cap > 0);

  // 定义一个新的哈希表Map2，并拷贝所有的键值对
  HashMap Map2 = {};
  // 为Map2分配足够的内存来存储Cap个桶
  Map2.Buckets = calloc(Cap, sizeof(HashEntry));
  // 设置Map2的容量为Cap
  Map2.Capacity = Cap;

  // 遍历当前哈希表的每个桶
  for (int I = 0; I < Map->Capacity; I++) {
    // 指向当前桶
    HashEntry *Ent = &Map->Buckets[I];
    // 如果当前桶中存在键值对并且未被标记删除
    if (Ent->Key && Ent->Key != TOMBSTONE)
      // 将该键值对放入新的哈希表Map2中
      hashmapPut2(&Map2, Ent->Key, Ent->KeyLen, Ent->Val);
  }

  // 断言Map2中的键值对数量等于NKeys
  assert(Map2.Used == NKeys);
  // 用新的哈希表Map2替换旧的哈希表
  *Map = Map2;
}

// 判断指定键是否匹配给定的键值对
static bool match(HashEntry *Ent, char *Key, int KeyLen) {
  // 键值对不为空，键值对未被标记删除，键值对与指定键长相同
  // 键值对的键与指定键相同
  return Ent->Key && Ent->Key != TOMBSTONE && Ent->KeyLen == KeyLen &&
         memcmp(Ent->Key, Key, KeyLen) == 0;
}

// 获取给定的键在哈希表中的键值对
static HashEntry *getEntry(HashMap *Map, char *Key, int KeyLen) {
  // 如果没有桶，则为空
  if (!Map->Buckets)
    return NULL;

  // 计算键对应的哈希值
  uint64_t Hash = fnvHash(Key, KeyLen);

  // 遍历哈希表中的所有桶
  for (int I = 0; I < Map->Capacity; I++) {
    // 开放寻址，当前位置存不下时，会在相邻位置存储
    // 如果当前位置没有匹配到，则加上I的偏移量，进行测试
    HashEntry *Ent = &Map->Buckets[(Hash + I) % Map->Capacity];
    // 若当前键值对的键，与所查找的键和键长相同
    if (match(Ent, Key, KeyLen))
      return Ent;
    // 所有的键值对都遍历完了，则返回空
    if (Ent->Key == NULL)
      return NULL;
  }
  unreachable();
}

// 若获取到键值对则返回，否则插入键值对后返回
static HashEntry *getOrInsertEntry(HashMap *Map, char *Key, int KeyLen) {
  if (!Map->Buckets) {
    // 如果哈希表没有初始化，则初始化INIT_SIZE个
    Map->Buckets = calloc(INIT_SIZE, sizeof(HashEntry));
    Map->Capacity = INIT_SIZE;
  } else if ((Map->Used * 100) / Map->Capacity >= HIGH_WATERMARK) {
    // 如果哈希表使用量超过了HIGH_WATERMARK，则重新进行哈希计算
    rehash(Map);
  }

  // 计算指定键的哈希值
  uint64_t Hash = fnvHash(Key, KeyLen);

  // 遍历所有的桶
  for (int I = 0; I < Map->Capacity; I++) {
    // 开放寻址，当前位置存不下时，会在相邻位置存储
    // 如果当前位置没有匹配到，则加上I的偏移量，进行测试
    HashEntry *Ent = &Map->Buckets[(Hash + I) % Map->Capacity];

    // 若当前键值对的键，与所查找的键和键长相同
    if (match(Ent, Key, KeyLen))
      return Ent;

    // 若当前键值对的键，被标记删除
    // 则赋值后使用该键值对
    if (Ent->Key == TOMBSTONE) {
      Ent->Key = Key;
      Ent->KeyLen = KeyLen;
      return Ent;
    }

    // 若当前键值对的键，为空
    // 则赋值后使用该键值对
    if (Ent->Key == NULL) {
      Ent->Key = Key;
      Ent->KeyLen = KeyLen;
      // 增加已使用量的计数
      Map->Used++;
      return Ent;
    }
  }
  unreachable();
}

// 查找哈希表中的键值对
void *hashmapGet(HashMap *Map, char *Key) {
  return hashmapGet2(Map, Key, strlen(Key));
}

void *hashmapGet2(HashMap *Map, char *Key, int KeyLen) {
  // 获取键值对
  HashEntry *Ent = getEntry(Map, Key, KeyLen);
  // 如果查找到键值对则返回，否则为空
  return Ent ? Ent->Val : NULL;
}

// 插入指定的键值对
void hashmapPut(HashMap *Map, char *Key, void *Val) {
  hashmapPut2(Map, Key, strlen(Key), Val);
}

void hashmapPut2(HashMap *Map, char *Key, int KeyLen, void *Val) {
  // 返回或创建键值对
  HashEntry *Ent = getOrInsertEntry(Map, Key, KeyLen);
  // 修改键值对的值
  Ent->Val = Val;
}

// 标记删除哈希表中的键值对
void hashmapDelete(HashMap *Map, char *Key) {
  hashmapDelete2(Map, Key, strlen(Key));
}

void hashmapDelete2(HashMap *Map, char *Key, int KeyLen) {
  // 查找指定的键值对
  HashEntry *Ent = getEntry(Map, Key, KeyLen);
  // 若键值对存在，则标记删除
  if (Ent)
    Ent->Key = TOMBSTONE;
}

// 用于哈希功能测试的函数
void hashmapTest(void) {
  // 新建一个容量为0的哈希表
  HashMap *Map = calloc(1, sizeof(HashMap));

  // 0  -  1000  -  1500  -  1600  -  2000  -  5000  -  6000  -  7000
  // ｜ 存在 ｜  删除  ｜  存在  ｜  删除  ｜  存在  ｜   空   ｜  存在   ｜
  for (int I = 0; I < 5000; I++)
    hashmapPut(Map, format("key %d", I), (void *)(size_t)I);
  for (int I = 1000; I < 2000; I++)
    hashmapDelete(Map, format("key %d", I));
  for (int I = 1500; I < 1600; I++)
    hashmapPut(Map, format("key %d", I), (void *)(size_t)I);
  for (int I = 6000; I < 7000; I++)
    hashmapPut(Map, format("key %d", I), (void *)(size_t)I);

  for (int I = 0; I < 1000; I++)
    assert((size_t)hashmapGet(Map, format("key %d", I)) == I);
  for (int I = 1000; I < 1500; I++)
    assert(hashmapGet(Map, "no such key") == NULL);
  for (int I = 1500; I < 1600; I++)
    assert((size_t)hashmapGet(Map, format("key %d", I)) == I);
  for (int I = 1600; I < 2000; I++)
    assert(hashmapGet(Map, "no such key") == NULL);
  for (int I = 2000; I < 5000; I++)
    assert((size_t)hashmapGet(Map, format("key %d", I)) == I);
  for (int I = 5000; I < 6000; I++)
    assert(hashmapGet(Map, "no such key") == NULL);
  for (int I = 6000; I < 7000; I++)
    hashmapPut(Map, format("key %d", I), (void *)(size_t)I);

  assert(hashmapGet(Map, "no such key") == NULL);
  printf("OK\n");
}
