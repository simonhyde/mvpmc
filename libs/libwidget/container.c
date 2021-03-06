/*
 *  Copyright (C) 2004-2006, Jon Gettler
 *  http://www.mvpmc.org/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mvp_widget.h"
#include "widget.h"

static void
expose(mvp_widget_t *widget)
{
	int i;

	GrClearArea(widget->wid, 0, 0, 0, 0, 1);

	for (i=0; i<widget->data.container.max_items; i++)
		if (widget->data.container.widgets[i])
			mvpw_expose(widget->data.container.widgets[i]);
}

static void
destroy(mvp_widget_t *widget)
{
	int i;

	for (i=0; i<widget->data.container.max_items; i++)
		if (widget->data.container.widgets[i])
			mvpw_destroy(widget->data.container.widgets[i]);

	if (widget->data.container.widgets)
		free(widget->data.container.widgets);
}

static void
key(mvp_widget_t *widget, char c)
{
	if (widget->parent && widget->parent->key)
		widget->parent->key(widget->parent, c);
}

static int
add_child(mvp_widget_t *parent, mvp_widget_t *child)
{
	int i = 0;
	mvp_widget_t **list;

	list = parent->data.container.widgets;

	if (parent->data.container.nitems == parent->data.container.max_items) {
		list = realloc(list, sizeof(*list)*(parent->data.container.max_items+32));
		if (list)
			parent->data.container.widgets = list;
		else
			return -1;
		memset(list+parent->data.container.max_items, 0, sizeof(*list)*32);
		parent->data.container.max_items += 32;
	}

	for (i=0; i<parent->data.container.max_items; i++) {
		if (parent->data.container.widgets[i] == NULL) {
			parent->data.container.widgets[i] = child;
			parent->data.container.nitems++;
			return 0;
		}
	}

	return -1;
}

static int
remove_child(mvp_widget_t *parent, mvp_widget_t *child)
{
	int i = 0;

	for (i=0; i<parent->data.container.max_items; i++) {
		if (parent->data.container.widgets[i] == child) {
			parent->data.container.widgets[i] = NULL;
			parent->data.container.nitems--;
			return 0;
		}
	}
	
	return -1;
}

mvp_widget_t*
mvpw_create_container(mvp_widget_t *parent,
		      int x, int y, int w, int h,
		      uint32_t bg, uint32_t border_color, int border_size)
{
	mvp_widget_t *widget;

	widget = mvpw_create(parent, x, y, w, h, bg,
			     border_color, border_size);

	widget->event_mask &= ~GR_EVENT_MASK_EXPOSURE;
	GrSelectEvents(widget->wid, widget->event_mask);

	if (widget == NULL)
		return NULL;

	widget->type = MVPW_CONTAINER;
	widget->expose = expose;
	widget->key = key;
	widget->add_child = add_child;
	widget->remove_child = remove_child;
	widget->destroy = destroy;

	memset(&widget->data, 0, sizeof(widget->data));

	return widget;
}
