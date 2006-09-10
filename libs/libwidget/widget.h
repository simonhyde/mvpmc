/*
 *  $Id$
 *
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
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

#ifndef WIDGET_H
#define WIDGET_H

#include "nano-X.h"

typedef enum {
	MVPW_UNKNOWN,
	MVPW_ROOT,
	MVPW_TEXT,
	MVPW_MENU,
	MVPW_CONTAINER,
	MVPW_IMAGE,
	MVPW_GRAPH,
	MVPW_CHECKBOX,
	MVPW_BITMAP,
	MVPW_DIALOG,
	MVPW_SURFACE,
	MVPW_ARRAY,
} mvpw_id_t;

typedef struct {
	int		  nitems;
	int		  max_items;
	mvp_widget_t	**widgets;
} mvpw_container_t;

typedef struct {
	char		*str;
	int		 wrap;
	int		 pack;
	int		 justify;
	int		 margin;
	GR_FONT_ID	 font;
	GR_COLOR	 fg;
	GR_COLOR	 text_bg;
	int		 rounded;
	int		 buflen;
	int		 utf8;

	void (*callback_key)(char*, char);
} mvpw_text_t;

typedef struct {
	int rows;
	int cols;
	int col_label_height;
	int row_label_width;
	int cell_height;
	int cell_width;
	int dirty;
	uint32_t array_border;
	int border_size;
	int hilite_x;
	int hilite_y;
	/* Default attributes for cells and headers */
	uint32_t row_label_fg;
	uint32_t row_label_bg;
	uint32_t col_label_fg;
	uint32_t col_label_bg;
	uint32_t cell_fg;
	uint32_t cell_bg;
	uint32_t hilite_fg;
	uint32_t hilite_bg;
	int cell_rounded;
	mvp_widget_t **row_labels;
	mvp_widget_t **col_labels;
	mvp_widget_t **cells;
	int *cell_viz;
	char ** row_strings;
	char ** col_strings;
	char ** cell_strings;
	/* An index identifying the corresponding cell that is provided
	 * by the user and returned to the user when the cell is selected.
	 */
	void ** cell_data;

	/* Placeholder for now, might need others */
	void (*callback_key)(char*, char);
	void (*scroll_callback)(mvp_widget_t *widget, int direction);
} mvpw_array_t;

typedef struct {
	char		*title;
	int		 nitems;
	int		 max_items;
	int		 current;
	GR_FONT_ID	 font;
	GR_COLOR	 fg;
	GR_COLOR	 bg;
	GR_COLOR	 hilite_fg;
	GR_COLOR	 hilite_bg;
	GR_COLOR	 title_fg;
	GR_COLOR	 title_bg;
	int		 title_justify;
	int		 columns;
	int		 rows;
	int		 checkboxes;
	int		 rounded;
	int		 margin;
	int		 utf8;

	mvp_widget_t	*title_widget;
	mvp_widget_t	*first_widget;

	struct menu_item_s {
		char		 *label;
		void		 *key;
		void		(*select)(mvp_widget_t*, char*, void*);
		void		(*hilite)(mvp_widget_t*, char*, void*, int);
		void		(*destroy)(mvp_widget_t*, char*, void*);
		mvp_widget_t	 *widget;
		mvp_widget_t	 *checkbox;
		int		  selectable;
		int		  checked;
		GR_COLOR	  fg;
		GR_COLOR	  bg;
		GR_COLOR	  checkbox_fg;
	} *items;
} mvpw_menu_t;

typedef struct {
	GR_IMAGE_ID	  iid;
	GR_WINDOW_ID	  wid;
	GR_WINDOW_ID	  pid;
	int		  stretch;
	char		 *file;
} mvpw_image_t;

typedef struct {
	GR_COLOR	 fg;
	int		 min;
	int		 max;
	int		 current;
	int		 gradient;
	GR_COLOR	 left;
	GR_COLOR	 right;
} mvpw_graph_t;

typedef struct {
	GR_COLOR	 fg;
	int		 checked;
} mvpw_checkbox_t;

typedef struct {
	int		 colors;
	char		*image;
} mvpw_bitmap_t;

typedef struct {
	int		 modal;
	GR_COLOR	 fg;
	GR_COLOR	 title_fg;
	GR_COLOR	 title_bg;
	int		 font;
	mvp_widget_t	*title_widget;
	mvp_widget_t	*text_widget;
	mvp_widget_t	*image_widget;
	int		 margin;
	int		 utf8;
} mvpw_dialog_t;

typedef struct {
	int		pixtype;
	int		wid;
	MWPIXELVAL	foreground;
	int		fd;
} mvpw_surface_t;

struct mvp_widget_s {
	mvpw_id_t	 type;
	GR_WINDOW_ID	 wid;
	GR_TIMER_ID	 tid;
	mvp_widget_t	*parent;
	GR_COORD	 x;
	GR_COORD	 y;
	unsigned int	 width;
	unsigned int	 height;
	GR_COLOR	 bg;
	GR_COLOR	 border_color;
	int		 border_size;
	GR_EVENT_MASK	 event_mask;
	mvp_widget_t	*attach[4];
	mvp_widget_t	*above;
	mvp_widget_t	*below;
	void 		*user_data;

	void (*resize)(mvp_widget_t*);
	int (*add_child)(mvp_widget_t*, mvp_widget_t*);
	int (*remove_child)(mvp_widget_t*, mvp_widget_t*);

	void (*destroy)(mvp_widget_t*);
	void (*expose)(mvp_widget_t*);
	void (*key)(mvp_widget_t*, char);
	void (*timer)(mvp_widget_t*);
	void (*fdinput)(mvp_widget_t*, int);
	void (*show)(mvp_widget_t*, int);

	void (*callback_destroy)(mvp_widget_t*);
	void (*callback_expose)(mvp_widget_t*);
	void (*callback_key)(mvp_widget_t*, char);
	void (*callback_timer)(mvp_widget_t*);
	void (*callback_fdinput)(mvp_widget_t*, int);

	union {
		mvpw_text_t		text;
		mvpw_array_t  array;
		mvpw_menu_t		menu;
		mvpw_container_t	container;
		mvpw_image_t		image;
		mvpw_graph_t		graph;
		mvpw_checkbox_t		checkbox;
		mvpw_bitmap_t		bitmap;
		mvpw_dialog_t		dialog;
		mvpw_surface_t		surface;
	} data;
};

extern mvp_widget_t* mvpw_create(mvp_widget_t *parent, GR_COORD x, GR_COORD y,
				 unsigned int width, unsigned int height,
				 GR_COLOR bg,
				 GR_COLOR border_color, int border_size);
extern void mvpw_destroy(mvp_widget_t *widget);

#endif /* WIDGET_H */
