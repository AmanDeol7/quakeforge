#include "Array.h"

@implementation Array

- (id) init
{
	count = size = 0;
	incr = 16;
	array = NIL;
	return self;
}

- (id) initWithIncrement: (integer) inc
{
	count = 0;
	size = incr = inc;
	array = (id []) obj_malloc (inc * @sizeof (id));
	return self;
}

- (void) free
{
	local integer i;
	for (i = 0; i < count; i++)
		[array[i] free];
	obj_free (array);
	[super free];
}

- (id) getItemAt: (integer) index
{
	if (index == -1)
		index = count - 1;
	if (index < 0 || index >= count)
		return NIL;
	return array[index];
}

- (void) setItemAt: (integer) index item: (id) item
{
	if (index == -1)
		index = count - 1;
	if (index < 0 || index >= count)
		return;
	array[index] = item;
}

- (void) addItem: (id) item
{
	if (count == size) {
		size += incr;
		array = (id [])obj_realloc (array, size * @sizeof (id));
	}
	array[count++] = item;
}

- (void) removeItem: (id) item
{
	local integer i, n;

	for (i = 0; i < count; i++)
		if (array[i] == item) {
			count--;
			for (n = i; n < count; n++)
				array[n] = array[n + 1];
		}
	return;
}

- (id) removeItemAt: (integer) index
{
	local integer i;
	local id item;

	if (index == -1)
		index = count -1;
	if (index < 0 || index >= count)
		return NIL;
	item = array[index];
	count--;
	for (i = index; i < count; i++)
		array[i] = array[i + 1];
	return item;
}

- (id) insertItemAt: (integer) index item:(id) item
{
	local integer i;
	if (index == -1)
		index = count -1;
	if (index < 0 || index >= count)
		return NIL;
	if (count == size) {
		size += incr;
		array = (id [])obj_realloc (array, size * @sizeof (id));
	}
	for (i = count; i > index; i--)
		array[i] = array[i - 1];
	array[index] = item;
	count++;
	return item;
}

- (integer) count
{
	return count;
}

-(void)makeObjectsPerformSelector:(SEL)selector
{
	local integer i;
	for (i = 0; i < count; i++)
		[array[i] perform:selector];
}

-(void)makeObjectsPerformSelector:(SEL)selector withObject:(id)arg
{
	local integer i;
	for (i = 0; i < count; i++)
		[array[i] perform:selector withObject:arg];
}

@end
