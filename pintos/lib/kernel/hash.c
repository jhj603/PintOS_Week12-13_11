/* 해시 테이블.

   이 자료구조는 Pintos 프로젝트 3를 위한 Tour of Pintos에
   자세히 문서화되어 있습니다.

   기본 정보는 hash.h를 참고하십시오. */

#include "hash.h"
#include "../debug.h"
#include "threads/malloc.h"

#define list_elem_to_hash_elem(LIST_ELEM)                       \
	list_entry(LIST_ELEM, struct hash_elem, list_elem)

static struct list *find_bucket (struct hash *, struct hash_elem *);
static struct hash_elem *find_elem (struct hash *, struct list *,
		struct hash_elem *);
static void insert_elem (struct hash *, struct list *, struct hash_elem *);
static void remove_elem (struct hash *, struct hash_elem *);
static void rehash (struct hash *);

/* 해시 테이블 H를 초기화하여 HASH로 해시 값을 계산하고
   보조 데이터 AUX를 사용해 LESS로 해시 원소를 비교할 수 있게 합니다. */
bool
hash_init (struct hash *h,
		hash_hash_func *hash, hash_less_func *less, void *aux) {
	h->elem_cnt = 0;
	h->bucket_cnt = 4;
	h->buckets = malloc (sizeof *h->buckets * h->bucket_cnt);
	h->hash = hash;
	h->less = less;
	h->aux = aux;

	if (h->buckets != NULL) {
		hash_clear (h, NULL);
		return true;
	} else
		return false;
}

/* H에서 모든 원소를 제거합니다.

   DESTRUCTOR가 null이 아니면 해시의 각 원소에 대해 호출됩니다.
   필요하다면 DESTRUCTOR가 해시 원소에 사용된 메모리를 해제할 수 있습니다.
   그러나 hash_clear()가 실행되는 동안 hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), hash_delete()를 포함한 어떤 함수로든
   해시 테이블 H를 수정하면, DESTRUCTOR 내부든 다른 곳이든 정의되지 않은
   동작이 발생합니다. */
void
hash_clear (struct hash *h, hash_action_func *destructor) {
	size_t i;

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];

		if (destructor != NULL)
			while (!list_empty (bucket)) {
				struct list_elem *list_elem = list_pop_front (bucket);
				struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
				destructor (hash_elem, h->aux);
			}

		list_init (bucket);
	}

	h->elem_cnt = 0;
}

/* 해시 테이블 H를 파괴합니다.

   DESTRUCTOR가 null이 아니면 먼저 해시의 각 원소에 대해 호출됩니다.
   필요하다면 DESTRUCTOR가 해시 원소에 사용된 메모리를 해제할 수 있습니다.
   그러나 hash_clear()가 실행되는 동안 hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), hash_delete()를 포함한 어떤 함수로든
   해시 테이블 H를 수정하면, DESTRUCTOR 내부든 다른 곳이든 정의되지 않은
   동작이 발생합니다. */
void
hash_destroy (struct hash *h, hash_action_func *destructor) {
	if (destructor != NULL)
		hash_clear (h, destructor);
	free (h->buckets);
}

/* NEW를 해시 테이블 H에 삽입하고, 동일한 원소가 없으면 null 포인터를 반환합니다.
   동일한 원소가 이미 존재하면 NEW를 삽입하지 않고 그 원소를 반환합니다. */
struct hash_elem *
hash_insert (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new);
	struct hash_elem *old = find_elem (h, bucket, new);

	if (old == NULL)
		insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* NEW를 해시 테이블 H에 삽입하며, 동일한 원소가 있으면 그 원소를 대체하고 반환합니다. */
struct hash_elem *
hash_replace (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new);
	struct hash_elem *old = find_elem (h, bucket, new);

	if (old != NULL)
		remove_elem (h, old);
	insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* 해시 테이블 H에서 E와 동일한 원소를 찾아 반환하며, 없으면 null 포인터를 반환합니다. */
struct hash_elem *
hash_find (struct hash *h, struct hash_elem *e) {
	return find_elem (h, find_bucket (h, e), e);
}

/* 해시 테이블 H에서 E와 동일한 원소를 찾아 제거하고 반환합니다.
   동일한 원소가 없으면 null 포인터를 반환합니다.

   해시 테이블의 원소가 동적으로 할당되었거나 별도의 자원을 소유한다면,
   그 자원을 해제하는 책임은 호출자에게 있습니다. */
struct hash_elem *
hash_delete (struct hash *h, struct hash_elem *e) {
	struct hash_elem *found = find_elem (h, find_bucket (h, e), e);
	if (found != NULL) {
		remove_elem (h, found);
		rehash (h);
	}
	return found;
}

/* 해시 테이블 H의 각 원소에 대해 임의의 순서로 ACTION을 호출합니다.
   hash_apply()가 실행되는 동안 hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), hash_delete()와 같은 함수를 사용해
   해시 테이블 H를 수정하면 ACTION 내부든 다른 곳이든 정의되지 않은
   동작이 발생합니다. */
void
hash_apply (struct hash *h, hash_action_func *action) {
	size_t i;

	ASSERT (action != NULL);

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];
		struct list_elem *elem, *next;

		for (elem = list_begin (bucket); elem != list_end (bucket); elem = next) {
			next = list_next (elem);
			action (list_elem_to_hash_elem (elem), h->aux);
		}
	}
}

/* 해시 테이블 H를 순회하기 위해 I를 초기화합니다.

   순회 예시:

   struct hash_iterator i;

   hash_first (&i, h);
   while (hash_next (&i))
   {
   struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);
   ...f에 대해 무언가를 수행...
   }

   순회 중에 hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 등의 함수를 사용해 해시 테이블 H를 수정하면
   모든 반복자가 무효화됩니다. */
void
hash_first (struct hash_iterator *i, struct hash *h) {
	ASSERT (i != NULL);
	ASSERT (h != NULL);

	i->hash = h;
	i->bucket = i->hash->buckets;
	i->elem = list_elem_to_hash_elem (list_head (i->bucket));
}

/* I를 해시 테이블의 다음 원소로 이동시키고 그 원소를 반환합니다.
   남은 원소가 없으면 null 포인터를 반환합니다. 반환 순서는 임의입니다.

   순회 중에 hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 등의 함수를 사용해 해시 테이블 H를 수정하면
   모든 반복자가 무효화됩니다. */
struct hash_elem *
hash_next (struct hash_iterator *i) {
	ASSERT (i != NULL);

	i->elem = list_elem_to_hash_elem (list_next (&i->elem->list_elem));
	while (i->elem == list_elem_to_hash_elem (list_end (i->bucket))) {
		if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt) {
			i->elem = NULL;
			break;
		}
		i->elem = list_elem_to_hash_elem (list_begin (i->bucket));
	}

	return i->elem;
}

/* 해시 테이블 순회에서 현재 원소를 반환하거나, 끝에 도달하면 null 포인터를 반환합니다.
   hash_first()를 호출한 뒤 hash_next() 전에 호출하면 정의되지 않은 동작입니다. */
struct hash_elem *
hash_cur (struct hash_iterator *i) {
	return i->elem;
}

/* H에 있는 원소 수를 반환합니다. */
size_t
hash_size (struct hash *h) {
	return h->elem_cnt;
}

/* H에 원소가 없으면 true, 아니면 false를 반환합니다. */
bool
hash_empty (struct hash *h) {
	return h->elem_cnt == 0;
}

/* 32비트 워드 크기용 Fowler-Noll-Vo 해시 상수. */
#define FNV_64_PRIME 0x00000100000001B3UL
#define FNV_64_BASIS 0xcbf29ce484222325UL

/* BUF의 SIZE 바이트에 대한 해시 값을 반환합니다. */
uint64_t
hash_bytes (const void *buf_, size_t size) {
	/* 바이트용 Fowler-Noll-Vo 32비트 해시. */
	const unsigned char *buf = buf_;
	uint64_t hash;

	ASSERT (buf != NULL);

	hash = FNV_64_BASIS;
	while (size-- > 0)
		hash = (hash * FNV_64_PRIME) ^ *buf++;

	return hash;
}

/* 문자열 S에 대한 해시 값을 반환합니다. */
uint64_t
hash_string (const char *s_) {
	const unsigned char *s = (const unsigned char *) s_;
	uint64_t hash;

	ASSERT (s != NULL);

	hash = FNV_64_BASIS;
	while (*s != '\0')
		hash = (hash * FNV_64_PRIME) ^ *s++;

	return hash;
}

/* 정수 I에 대한 해시 값을 반환합니다. */
uint64_t
hash_int (int i) {
	return hash_bytes (&i, sizeof i);
}

/* E가 속하는 H의 버킷을 반환합니다. */
static struct list *
find_bucket (struct hash *h, struct hash_elem *e) {
	size_t bucket_idx = h->hash (e, h->aux) & (h->bucket_cnt - 1);
	return &h->buckets[bucket_idx];
}

/* H에서 BUCKET을 탐색해 E와 동일한 해시 원소를 찾습니다.
   찾으면 해당 원소를, 그렇지 않으면 null 포인터를 반환합니다. */
static struct hash_elem *
find_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	struct list_elem *i;

	for (i = list_begin (bucket); i != list_end (bucket); i = list_next (i)) {
		struct hash_elem *hi = list_elem_to_hash_elem (i);
		if (!h->less (hi, e, h->aux) && !h->less (e, hi, h->aux))
			return hi;
	}
	return NULL;
}

/* X의 최하위 1비트를 0으로 만든 값을 반환합니다. */
static inline size_t
turn_off_least_1bit (size_t x) {
	return x & (x - 1);
}

/* X가 2의 거듭제곱이면 true, 아니면 false를 반환합니다. */
static inline size_t
is_power_of_2 (size_t x) {
	return x != 0 && turn_off_least_1bit (x) == 0;
}

/* 버킷당 원소 비율. */
#define MIN_ELEMS_PER_BUCKET  1 /* 버킷당 원소 < 1: 버킷 수를 줄입니다. */
#define BEST_ELEMS_PER_BUCKET 2 /* 이상적인 버킷당 원소 수. */
#define MAX_ELEMS_PER_BUCKET  4 /* 버킷당 원소 > 4: 버킷 수를 늘립니다. */

/* 해시 테이블 H의 버킷 수를 이상적인 값에 맞게 조정합니다.
   메모리 부족으로 실패할 수 있지만, 이 경우 해시 접근이 덜 효율적일 뿐
   계속 사용은 가능합니다. */
static void
rehash (struct hash *h) {
	size_t old_bucket_cnt, new_bucket_cnt;
	struct list *new_buckets, *old_buckets;
	size_t i;

	ASSERT (h != NULL);

	/* 이후 사용을 위해 기존 버킷 정보를 저장합니다. */
	old_buckets = h->buckets;
	old_bucket_cnt = h->bucket_cnt;

	/* 현재 사용할 버킷 수를 계산합니다.
	   BEST_ELEMS_PER_BUCKET마다 하나의 버킷을 두고자 합니다.
	   최소 네 개 이상의 버킷이 필요하며, 버킷 수는 2의 거듭제곱이어야 합니다. */
	new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;
	if (new_bucket_cnt < 4)
		new_bucket_cnt = 4;
	while (!is_power_of_2 (new_bucket_cnt))
		new_bucket_cnt = turn_off_least_1bit (new_bucket_cnt);

	/* 버킷 수가 변하지 않으면 아무 것도 하지 않습니다. */
	if (new_bucket_cnt == old_bucket_cnt)
		return;

	/* 새 버킷을 할당하고 비어 있는 상태로 초기화합니다. */
	new_buckets = malloc (sizeof *new_buckets * new_bucket_cnt);
	if (new_buckets == NULL) {
		/* 할당에 실패했습니다. 이는 해시 테이블 사용이 덜 효율적이라는 의미지만,
		   여전히 사용할 수 있으므로 오류로 처리할 필요는 없습니다. */
		return;
	}
	for (i = 0; i < new_bucket_cnt; i++)
		list_init (&new_buckets[i]);

	/* 새 버킷 정보를 적용합니다. */
	h->buckets = new_buckets;
	h->bucket_cnt = new_bucket_cnt;

	/* 기존 각 원소를 알맞은 새 버킷으로 옮깁니다. */
	for (i = 0; i < old_bucket_cnt; i++) {
		struct list *old_bucket;
		struct list_elem *elem, *next;

		old_bucket = &old_buckets[i];
		for (elem = list_begin (old_bucket);
				elem != list_end (old_bucket); elem = next) {
			struct list *new_bucket
				= find_bucket (h, list_elem_to_hash_elem (elem));
			next = list_next (elem);
			list_remove (elem);
			list_push_front (new_bucket, elem);
		}
	}

	free (old_buckets);
}

/* E를 버킷(해시 테이블 H)에 삽입합니다. */
static void
insert_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	h->elem_cnt++;
	list_push_front (bucket, &e->list_elem);
}

/* 해시 테이블 H에서 E를 제거합니다. */
static void
remove_elem (struct hash *h, struct hash_elem *e) {
	h->elem_cnt--;
	list_remove (&e->list_elem);
}

