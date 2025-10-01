#ifndef __LIB_KERNEL_HASH_H
#define __LIB_KERNEL_HASH_H

/* 해시 테이블.
 *
 * 이 자료구조는 Pintos 프로젝트 3를 위한 Tour of Pintos에서
 * 자세히 설명되어 있습니다.
 *
 * 이 구조는 체이닝을 사용하는 표준 해시 테이블입니다. 테이블에서
 * 원소를 찾기 위해 원소의 데이터에 대해 해시 함수를 계산하고, 그
 * 결과를 이중 연결 리스트 배열의 인덱스로 사용한 뒤 리스트를
 * 선형으로 탐색합니다.
 *
 * 체인 리스트는 동적 할당을 사용하지 않습니다. 대신 해시에 들어갈 수
 * 있는 모든 구조체는 struct hash_elem 멤버를 포함해야 합니다. 모든
 * 해시 함수는 이러한 `struct hash_elem`에 대해 동작합니다. hash_entry
 * 매크로는 struct hash_elem을 다시 그것을 포함하는 구조체 객체로
 * 변환할 수 있게 해줍니다. 이는 연결 리스트 구현에서 사용하는 것과
 * 동일한 기법입니다. 자세한 내용은 lib/kernel/list.h를 참고하십시오. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "list.h"

/* 해시 원소. */
struct hash_elem {
	struct list_elem list_elem;
};

/* 해시 원소 HASH_ELEM에 대한 포인터를, HASH_ELEM이 포함된
 * 구조체의 포인터로 변환합니다. 외부 구조체 이름 STRUCT와 해시 원소의
 * 멤버 이름 MEMBER를 넘겨야 합니다. 예시는 파일 맨 위의 긴 주석을
 * 참고하십시오. */
#define hash_entry(HASH_ELEM, STRUCT, MEMBER)                   \
	((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem        \
		- offsetof (STRUCT, MEMBER.list_elem)))

/* 해시 원소 E와 보조 데이터 AUX를 받아 해시 값을 계산해 반환합니다. */
typedef uint64_t hash_hash_func (const struct hash_elem *e, void *aux);

/* 두 해시 원소 A와 B를 보조 데이터 AUX와 함께 비교합니다. A가 B보다 작으면 true, 그렇지 않으면 false를 반환합니다. */
typedef bool hash_less_func (const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux);

/* 해시 원소 E에 대해 보조 데이터 AUX를 사용해 동작을 수행합니다. */
typedef void hash_action_func (struct hash_elem *e, void *aux);

/* 해시 테이블. */
struct hash {
	size_t elem_cnt;            /* 테이블에 있는 원소 수. */
	size_t bucket_cnt;          /* 버킷 수(2의 거듭제곱). */
	struct list *buckets;       /* 'bucket_cnt' 개의 리스트 배열. */
	hash_hash_func *hash;       /* 해시 함수. */
	hash_less_func *less;       /* 비교 함수. */
	void *aux;                  /* 'hash'와 'less'에 전달할 보조 데이터. */
};

/* 해시 테이블 반복자. */
struct hash_iterator {
	struct hash *hash;          /* 해시 테이블. */
	struct list *bucket;        /* 현재 버킷. */
	struct hash_elem *elem;     /* 현재 버킷의 현재 해시 원소. */
};

/* 기본 생명 주기. */
bool hash_init (struct hash *, hash_hash_func *, hash_less_func *, void *aux);
void hash_clear (struct hash *, hash_action_func *);
void hash_destroy (struct hash *, hash_action_func *);

/* 검색, 삽입, 삭제. */
struct hash_elem *hash_insert (struct hash *, struct hash_elem *);
struct hash_elem *hash_replace (struct hash *, struct hash_elem *);
struct hash_elem *hash_find (struct hash *, struct hash_elem *);
struct hash_elem *hash_delete (struct hash *, struct hash_elem *);

/* 반복. */
void hash_apply (struct hash *, hash_action_func *);
void hash_first (struct hash_iterator *, struct hash *);
struct hash_elem *hash_next (struct hash_iterator *);
struct hash_elem *hash_cur (struct hash_iterator *);

/* 정보. */
size_t hash_size (struct hash *);
bool hash_empty (struct hash *);

/* 샘플 해시 함수. */
uint64_t hash_bytes (const void *, size_t);
uint64_t hash_string (const char *);
uint64_t hash_int (int);

#endif /* lib/kernel/hash.h 끝 */
