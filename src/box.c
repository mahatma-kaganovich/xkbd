/*
   xkbd - xlib based onscreen keyboard.

   Copyright (C) 2001 Matthew Allum
   Changes (C) 2017+ Denis Kaganovich

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#include <stdio.h>
#include <stdlib.h>

#include "structs.h"

#define WIDGET_BOX    0
#define WIDGET_BUTTON 1

box* box_new(void)
{
  return calloc1(box);
}

static void box_add_(box *bx, void *b, int type) {
  list *new  = malloc1(list);
  new->next = NULL;
  new->data = b;
  new->type = WIDGET_BUTTON;

  if (bx->root_kid == NULL) { /* new list */
      bx->root_kid = new;
  } else {
      bx->tail_kid->next = new;
  }
  bx->tail_kid = new;
}

void box_add_button(box *bx, button *but)
{
  box_add_(bx,but,WIDGET_BUTTON);
  but->parent = bx; /* set its parent */
}

void box_add_box(box *bx, box *b)
{
  box_add_(bx,b,WIDGET_BOX);
  b->parent = bx; /* set its parent */
}

void box_list_contents(box *bx)
{
  list *ptr = bx->root_kid;
  if (ptr == NULL) return;
  while (ptr != NULL)
    {
      /*      if (ptr->type == WIDGET_BUTTON)
	      printf("listing %s\n", ((button *)ptr->data)->txt); */
      printf("size is %i\n", (int)sizeof(*(ptr->data)));
      ptr = ptr->next;
    }
}



