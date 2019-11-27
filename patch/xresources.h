#include <X11/Xresource.h>

/* macros */
#define LEN(a) (sizeof(a) / sizeof(a)[0])

/* Xresources preferences */
enum resource_type {
	STRING = 0,
	INTEGER = 1,
	FLOAT = 2
};

typedef struct {
	char *name;
	enum resource_type type;
	void *dst;
} ResourcePref;