#ifndef _MOS_RISCV_QUEUE_H_
#define _MOS_RISCV_QUEUE_H_

#define LIST_HEAD(name, type)                                                                      \
	struct name {                                                                                \
		struct type *lh_first;                                                                 \
	}

#define LIST_HEAD_INITIALIZER(head)                                                                \
	{ NULL }

#define LIST_ENTRY(type)                                                                           \
	struct {                                                                                     \
		struct type *le_next;                                                                  \
		struct type **le_prev;                                                                \
	}

#define LIST_EMPTY(head) ((head)->lh_first == NULL)
#define LIST_FIRST(head) ((head)->lh_first)
#define LIST_NEXT(elm, field) ((elm)->field.le_next)
#define LIST_FOREACH(var, head, field)                                                             \
	for ((var) = LIST_FIRST((head)); (var); (var) = LIST_NEXT((var), field))

#define LIST_INIT(head)                                                                            \
	do {                                                                                       \
		LIST_FIRST((head)) = NULL;                                                           \
	} while (0)

#define LIST_INSERT_HEAD(head, elm, field)                                                         \
	do {                                                                                       \
		if ((LIST_NEXT((elm), field) = LIST_FIRST((head))) != NULL)                          \
			LIST_FIRST((head))->field.le_prev = &LIST_NEXT((elm), field);                \
		LIST_FIRST((head)) = (elm);                                                          \
		(elm)->field.le_prev = &LIST_FIRST((head));                                          \
	} while (0)

#define LIST_REMOVE(elm, field)                                                                    \
	do {                                                                                       \
		if (LIST_NEXT((elm), field) != NULL)                                                 \
			LIST_NEXT((elm), field)->field.le_prev = (elm)->field.le_prev;               \
		*(elm)->field.le_prev = LIST_NEXT((elm), field);                                     \
	} while (0)

#define TAILQ_HEAD(name, type)                                                                     \
	struct name {                                                                                \
		struct type *tqh_first;                                                                \
		struct type **tqh_last;                                                                \
	}

#define TAILQ_ENTRY(type)                                                                          \
	struct {                                                                                     \
		struct type *tqe_next;                                                                  \
		struct type **tqe_prev;                                                                \
	}

#define TAILQ_INIT(head)                                                                           \
	do {                                                                                       \
		(head)->tqh_first = NULL;                                                            \
		(head)->tqh_last = &(head)->tqh_first;                                               \
	} while (0)

#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)
#define TAILQ_FOREACH(var, head, field)                                                            \
	for ((var) = TAILQ_FIRST((head)); (var); (var) = TAILQ_NEXT((var), field))

#define TAILQ_INSERT_HEAD(head, elm, field)                                                        \
	do {                                                                                       \
		if (((elm)->field.tqe_next = (head)->tqh_first) != NULL)                             \
			(head)->tqh_first->field.tqe_prev = &(elm)->field.tqe_next;                  \
		else                                                                                   \
			(head)->tqh_last = &(elm)->field.tqe_next;                                   \
		(head)->tqh_first = (elm);                                                           \
		(elm)->field.tqe_prev = &(head)->tqh_first;                                          \
	} while (0)

#define TAILQ_INSERT_TAIL(head, elm, field)                                                        \
	do {                                                                                       \
		(elm)->field.tqe_next = NULL;                                                        \
		(elm)->field.tqe_prev = (head)->tqh_last;                                            \
		*(head)->tqh_last = (elm);                                                           \
		(head)->tqh_last = &(elm)->field.tqe_next;                                           \
	} while (0)

#define TAILQ_REMOVE(head, elm, field)                                                             \
	do {                                                                                       \
		if (((elm)->field.tqe_next) != NULL)                                                 \
			(elm)->field.tqe_next->field.tqe_prev = (elm)->field.tqe_prev;               \
		else                                                                                   \
			(head)->tqh_last = (elm)->field.tqe_prev;                                    \
		*(elm)->field.tqe_prev = (elm)->field.tqe_next;                                      \
	} while (0)

#endif
