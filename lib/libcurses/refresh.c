/*	$NetBSD: refresh.c,v 1.129 2024/12/23 02:58:04 blymn Exp $	*/

/*
 * Copyright (c) 1981, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)refresh.c	8.7 (Berkeley) 8/13/94";
#else
__RCSID("$NetBSD: refresh.c,v 1.129 2024/12/23 02:58:04 blymn Exp $");
#endif
#endif				/* not lint */

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "curses.h"
#include "curses_private.h"

static void	domvcur(WINDOW *, int, int, int, int);
static void	putattr(__LDATA *);
static void	putattr_out(__LDATA *);
static int	putch(__LDATA *, __LDATA *, int, int);
static int	putchbr(__LDATA *, __LDATA *, __LDATA *, int, int);
static int	makech(int);
static void	quickch(void);
static void	scrolln(int, int, int, int, int);

static int	_wnoutrefresh(WINDOW *, int, int, int, int, int, int);

static int lineeq(__LDATA *, __LDATA *, size_t);

#define	CHECK_INTERVAL		5 /* Change N lines before checking typeahead */

#ifndef _CURSES_USE_MACROS

/*
 * refresh --
 *	Make the current screen look like "stdscr" over the area covered by
 *	stdscr.
 */
int
refresh(void)
{

	return wrefresh(stdscr);
}

#endif

/*
 * wnoutrefresh --
 *	Add the contents of "win" to the virtual window.
 */
int
wnoutrefresh(WINDOW *win)
{

	__CTRACE(__CTRACE_REFRESH,
	    "wnoutrefresh: win %p, begy %d, begx %d, maxy %d, maxx %d\n",
	    win, win->begy, win->begx, win->maxy, win->maxx);

	return _wnoutrefresh(win, 0, 0, win->begy, win->begx,
	    win->maxy, win->maxx);
}

/*
 * pnoutrefresh --
 *	Add the contents of "pad" to the virtual window.
 */
int
pnoutrefresh(WINDOW *pad, int pbegy, int pbegx, int sbegy, int sbegx,
	     int smaxy, int smaxx)
{
	int pmaxy, pmaxx;

	__CTRACE(__CTRACE_REFRESH, "pnoutrefresh: pad %p, flags 0x%08x\n",
	    pad, pad->flags);
	__CTRACE(__CTRACE_REFRESH,
	    "pnoutrefresh: (%d, %d), (%d, %d), (%d, %d)\n",
	    pbegy, pbegx, sbegy, sbegx, smaxy, smaxx);

	if (__predict_false(pad == NULL))
		return ERR;

	/* SUS says if these are negative, they should be treated as zero */
	if (pbegy < 0)
		pbegy = 0;
	if (pbegx < 0)
		pbegx = 0;
	if (sbegy < 0)
		sbegy = 0;
	if (sbegx < 0)
		sbegx = 0;

	/* Calculate rectangle on pad - used by _wnoutrefresh */
	pmaxy = pbegy + smaxy - sbegy + 1;
	pmaxx = pbegx + smaxx - sbegx + 1;

	/* Check rectangle fits in pad */
	if (pmaxy > pad->maxy - pad->begy)
		pmaxy = pad->maxy - pad->begy;
	if (pmaxx > pad->maxx - pad->begx)
		pmaxx = pad->maxx - pad->begx;

	if (smaxy - sbegy < 0 || smaxx - sbegx < 0 )
		return ERR;

	return _wnoutrefresh(pad,
	    pad->begy + pbegy, pad->begx + pbegx, pad->begy + sbegy,
	    pad->begx + sbegx, pmaxy, pmaxx);
}

/*
 * _wnoutrefresh --
 *	Does the grunt work for wnoutrefresh to the given screen.
 *	Copies the part of the window given by the rectangle
 *	(begy, begx) to (maxy, maxx) at screen position (wbegy, wbegx).
 */
static int
_wnoutrefresh(WINDOW *win, int begy, int begx, int wbegy, int wbegx,
              int maxy, int maxx)
{
	SCREEN *screen = win->screen;
	short	sy, wy, wx, y_off, x_off, mx, dy_off, dx_off, endy;
	int newy, newx;
#ifdef HAVE_WCHAR
	int i, tx;
	wchar_t ch;
#endif
	__LINE	*wlp, *vlp, *dwlp;
	WINDOW	*sub_win, *orig, *swin, *dwin;

	__CTRACE(__CTRACE_REFRESH, "_wnoutrefresh: win %p, flags 0x%08x\n",
	    win, win->flags);
	__CTRACE(__CTRACE_REFRESH,
	    "_wnoutrefresh: (%d, %d), (%d, %d), (%d, %d)\n",
	    begy, begx, wbegy, wbegx, maxy, maxx);

	if (__predict_false(win == NULL))
		return ERR;

	if (screen->curwin)
		return OK;

	swin = dwin = win;
	if (win->flags & __ISDERWIN)
		swin = win->orig;

	/*
	 * Recurse through any sub-windows, mark as dirty lines on the parent
	 * window that are dirty on the sub-window and clear the dirty flag on
	 * the sub-window.
	 */
	if (dwin->orig == 0) {
		orig = dwin;
		for (sub_win = dwin->nextp; sub_win != orig;
		    sub_win = sub_win->nextp) {
			if (sub_win->flags & __ISDERWIN)
				continue;
			__CTRACE(__CTRACE_REFRESH,
			    "wnout_refresh: win %p, sub_win %p\n",
			    orig, sub_win);
			for (sy = 0; sy < sub_win->maxy; sy++) {
				if (sub_win->alines[sy]->flags & __ISDIRTY) {
					orig->alines[sy + sub_win->begy - orig->begy]->flags
					    |= __ISDIRTY;
					sub_win->alines[sy]->flags
					    &= ~__ISDIRTY;
				}
				if (sub_win->alines[sy]->flags & __ISFORCED) {
					orig->alines[sy + sub_win->begy - orig->begy]->flags
					    |= __ISFORCED;
					sub_win->alines[sy]->flags
					    &= ~__ISFORCED;
				}
			}
		}
	}

	/* Check that cursor position on "win" is valid for "__virtscr" */
	newy = wbegy + dwin->cury - begy;
	newx = wbegx + dwin->curx - begx;
	if (begy <= dwin->cury && dwin->cury < maxy
	    && 0 <= newy && newy < screen->__virtscr->maxy)
		screen->__virtscr->cury = newy;
	if (begx <= dwin->curx && dwin->curx < maxx
	    && 0 <= newx && newx < screen->__virtscr->maxx)
		screen->__virtscr->curx = newx;

	/* Copy the window flags from "win" to "__virtscr" */
	if (dwin->flags & __CLEAROK) {
		if (dwin->flags & __FULLWIN)
			screen->__virtscr->flags |= __CLEAROK;
		dwin->flags &= ~__CLEAROK;
	}
	screen->__virtscr->flags &= ~__LEAVEOK;
	screen->__virtscr->flags |= dwin->flags;

	if ((dwin->flags & __ISDERWIN) != 0)
		endy = begy + maxy;
	else
		endy = maxy;

	for (wy = begy, y_off = wbegy, dy_off = 0; wy < endy &&
	    y_off < screen->__virtscr->maxy; wy++, y_off++, dy_off++)
	{
		wlp = swin->alines[wy];
		dwlp = dwin->alines[dy_off];
#ifdef DEBUG
		__CTRACE(__CTRACE_REFRESH,
		    "_wnoutrefresh: wy %d\tf %d\tl %d\tflags %x\n",
		    wy, *wlp->firstchp, *wlp->lastchp, wlp->flags);

		char *_wintype;

		if ((dwin->flags & __ISDERWIN) != 0)
			_wintype = "derwin";
		else
			_wintype = "dwin";

		__CTRACE(__CTRACE_REFRESH,
		    "_wnoutrefresh: %s wy %d\tf %d\tl %d\tflags %x\n",
		    _wintype, dy_off, *dwlp->firstchp, *dwlp->lastchp,
		    dwlp->flags);
		__CTRACE(__CTRACE_REFRESH,
		    "_wnoutrefresh: %s maxx %d\tch_off %d wlp %p\n",
		    _wintype, dwin->maxx, dwin->ch_off, wlp);
#endif
		if (((wlp->flags & (__ISDIRTY | __ISFORCED)) == 0) &&
		    ((dwlp->flags & (__ISDIRTY | __ISFORCED)) == 0))
			continue;
		__CTRACE(__CTRACE_REFRESH,
		    "_wnoutrefresh: line y_off %d (dy_off %d) is dirty\n",
		    y_off, dy_off);

		wlp = swin->alines[wy];
		vlp = screen->__virtscr->alines[y_off];

		if ((*wlp->firstchp < maxx + swin->ch_off &&
		    *wlp->lastchp >= swin->ch_off) ||
		    ((((dwin->flags & __ISDERWIN) != 0) &&
		     (*dwlp->firstchp < dwin->maxx + dwin->ch_off &&
		      *dwlp->lastchp >= dwin->ch_off))))
		{
			/* Set start column */
			wx = begx;
			x_off = wbegx;
			dx_off = 0;
			/*
			 * if a derwin then source change pointers aren't
			 * relevant.
			 */
			if ((dwin->flags & __ISDERWIN) != 0)
				mx = wx + maxx;
			else {
				if (*wlp->firstchp - swin->ch_off > 0) {
					wx += *wlp->firstchp - swin->ch_off;
					x_off += *wlp->firstchp - swin->ch_off;
				}
				mx = maxx;
				if (mx > *wlp->lastchp - swin->ch_off + 1)
					mx = *dwlp->lastchp - dwin->ch_off + 1;
				if (x_off + (mx - wx) > screen->__virtscr->maxx)
					mx -= (x_off + maxx) -
					    screen->__virtscr->maxx;
			}

			/* Copy line from "win" to "__virtscr". */
			while (wx < mx) {
				__CTRACE(__CTRACE_REFRESH,
				    "_wnoutrefresh: copy from %d, "
				    "%d to %d, %d: '%s', 0x%x, 0x%x",
				    wy, wx, y_off, x_off,
				    unctrl(wlp->line[wx].ch),
				    wlp->line[wx].attr, wlp->line[wx].cflags);
				__CTRACE(__CTRACE_REFRESH,
				    " (curdest %s, 0x%x, 0x%x)",
				    unctrl(vlp->line[x_off].ch),
				    vlp->line[x_off].attr,
				    vlp->line[x_off].cflags);
				/* Copy character */
				vlp->line[x_off].ch = wlp->line[wx].ch;
				/* Copy attributes  */
				vlp->line[x_off].attr = wlp->line[wx].attr;
				/* Copy character flags  */
				vlp->line[x_off].cflags = wlp->line[wx].cflags;
#ifdef HAVE_WCHAR
				vlp->line[x_off].wcols = wlp->line[wx].wcols;

				ch = wlp->line[wx].ch;
				for (tx = x_off + 1, i = wlp->line[wx].wcols - 1;
				    i > 0; i--, tx++) {
					vlp->line[tx].ch = ch;
					vlp->line[tx].wcols = i;
					vlp->line[tx].cflags =
					    CA_CONTINUATION;
				}
#endif /* HAVE_WCHAR */
				if (win->flags & __ISDERWIN) {
					dwlp->line[dx_off].ch =
						wlp->line[wx].ch;
					dwlp->line[dx_off].attr =
						wlp->line[wx].attr;
					dwlp->line[dx_off].cflags =
						wlp->line[wx].cflags;
#ifdef HAVE_WCHAR
					dwlp->line[dx_off].wcols =
						wlp->line[wx].wcols;

					for (tx = dx_off + 1, i = wlp->line[wx].wcols - 1;
					    i > 0; i--, tx++) {
						dwlp->line[tx].ch = ch;
						dwlp->line[tx].wcols = i;
						dwlp->line[tx].cflags =
						    CA_CONTINUATION;
					}
#endif /* HAVE_WCHAR */
				}

#ifdef HAVE_WCHAR
				if (wlp->line[wx].ch == win->bch) {
					vlp->line[x_off].ch = win->bch;
					vlp->line[x_off].wcols = win->wcols;
					vlp->line[x_off].cflags = CA_BACKGROUND;
					if (_cursesi_copy_nsp(win->bnsp,
							      &vlp->line[x_off])
					    == ERR)
						return ERR;
					if (win->flags & __ISDERWIN) {
						dwlp->line[dx_off].ch =
							win->bch;
						dwlp->line[dx_off].wcols =
						    win->wcols;
						dwlp->line[dx_off].cflags =
							wlp->line[wx].cflags;
						if (_cursesi_copy_nsp(win->bnsp,
						     &dwlp->line[dx_off])
						    == ERR)
							return ERR;
					}
				}
#endif /* HAVE_WCHAR */
				__CTRACE(__CTRACE_REFRESH, " = '%s', 0x%x\n",
				    unctrl(vlp->line[x_off].ch),
				    vlp->line[x_off].attr);
#ifdef HAVE_WCHAR
				x_off += wlp->line[wx].wcols;
				dx_off += wlp->line[wx].wcols;
				wx += wlp->line[wx].wcols;
#else
				wx++;
				x_off++;
				dx_off++;
#endif /* HAVE_WCHAR */
			}

			/* Set flags on "__virtscr" and unset on "win". */
			if (wlp->flags & __ISPASTEOL)
				vlp->flags |= __ISPASTEOL;
			else
				vlp->flags &= ~__ISPASTEOL;
			if (wlp->flags & __ISDIRTY)
				vlp->flags |= __ISDIRTY;
			if (wlp->flags & __ISFORCED)
				vlp->flags |= __ISFORCED;

#ifdef DEBUG
			__CTRACE(__CTRACE_REFRESH,
			    "win: firstch = %d, lastch = %d\n",
			    *wlp->firstchp, *wlp->lastchp);
			if (win->flags & __ISDERWIN) {
				__CTRACE(__CTRACE_REFRESH,
				    "derwin: fistch = %d, lastch = %d\n",
				    *dwlp->firstchp, *dwlp->lastchp);
			}
#endif
			/* Set change pointers on "__virtscr". */
			if (*vlp->firstchp >
				*wlp->firstchp + wbegx - win->ch_off)
					*vlp->firstchp =
					    *wlp->firstchp + wbegx - win->ch_off;
			if (*vlp->lastchp <
				*wlp->lastchp + wbegx - win->ch_off)
					*vlp->lastchp =
					    *wlp->lastchp + wbegx - win->ch_off;

			if (win->flags & __ISDERWIN) {
				if (*vlp->firstchp >
				    *dwlp->firstchp + wbegx - dwin->ch_off)
				{
					*vlp->firstchp =
					    *dwlp->firstchp + wbegx
						- dwin->ch_off;
					vlp->flags |= __ISDIRTY;
				}

				if (*vlp->lastchp <
				    *dwlp->lastchp + wbegx - dwin->ch_off)
				{
					*vlp->lastchp = *dwlp->lastchp
					    + wbegx - dwin->ch_off;
					vlp->flags |= __ISDIRTY;
				}
			}

			__CTRACE(__CTRACE_REFRESH,
			    "__virtscr: firstch = %d, lastch = %d\n",
			    *vlp->firstchp, *vlp->lastchp);
			/*
			 * Unset change pointers only if a window and we
			 * are not forcing a redraw. A pad can be displayed
			 * again without any of the contents changing.
			 */
			if (!((win->flags & __ISPAD)) ||
			    ((wlp->flags & __ISFORCED) == __ISFORCED))
			{
				/* Set change pointers on "win". */
				if (*wlp->firstchp >= win->ch_off)
					*wlp->firstchp = maxx + win->ch_off;
				if (*wlp->lastchp < maxx + win->ch_off)
					*wlp->lastchp = win->ch_off;
				if ((*wlp->lastchp < *wlp->firstchp) ||
				    (*wlp->firstchp >= maxx + win->ch_off) ||
				    (*wlp->lastchp <= win->ch_off)) {
					__CTRACE(__CTRACE_REFRESH,
					    "_wnoutrefresh: "
					    "line %d notdirty\n", wy);
					wlp->flags &= ~(__ISDIRTY | __ISFORCED);
				}
			}
		}
	}
	return OK;
}

/*
 * wrefresh --
 *	Make the current screen look like "win" over the area covered by
 *	win.
 */
int
wrefresh(WINDOW *win)
{
	int retval;
	int pbegx, pbegy;

	__CTRACE(__CTRACE_REFRESH, "wrefresh: win %p\n", win);

	if (__predict_false(win == NULL))
		return ERR;

	_cursesi_screen->curwin = (win == _cursesi_screen->curscr);
	if (!_cursesi_screen->curwin) {
		pbegx = pbegy = 0;
		if ((win->flags & __ISDERWIN) == __ISDERWIN) {
			pbegx = win->derx;
			pbegy = win->dery;
	__CTRACE(__CTRACE_REFRESH, "wrefresh: derwin, begy = %d, begx = %x\n",
		pbegy, pbegx);
		}
		retval = _wnoutrefresh(win, pbegy, pbegx, win->begy, win->begx,
		    win->maxy, win->maxx);
	} else
		retval = OK;
	if (retval == OK) {
		retval = doupdate();
		if (!(win->flags & __LEAVEOK)) {
			win->cury = max(0, curscr->cury - win->begy);
			win->curx = max(0, curscr->curx - win->begx);
		}
	}
	_cursesi_screen->curwin = 0;
	return retval;
}

 /*
 * prefresh --
 *	Make the current screen look like "pad" over the area coverd by
 *	the specified area of pad.
 */
int
prefresh(WINDOW *pad, int pbegy, int pbegx, int sbegy, int sbegx,
	int smaxy, int smaxx)
{
	int retval;

	__CTRACE(__CTRACE_REFRESH, "prefresh: pad %p, flags 0x%08x\n",
	    pad, pad->flags);

	if (__predict_false(pad == NULL))
		return ERR;

	/* Retain values in case pechochar() is called. */
	pad->pbegy = pbegy;
	pad->pbegx = pbegx;
	pad->sbegy = sbegy;
	pad->sbegx = sbegx;
	pad->smaxy = smaxy;
	pad->smaxx = smaxx;

	/* Use pnoutrefresh() to avoid duplicating code here */
	retval = pnoutrefresh(pad, pbegy, pbegx, sbegy, sbegx, smaxy, smaxx);
	if (retval == OK) {
		retval = doupdate();
		if (!(pad->flags & __LEAVEOK)) {
			pad->cury = max(0, pbegy + (curscr->cury - sbegy));
			pad->curx = max(0, pbegx + (curscr->curx - sbegx));
		}
	}
	return retval;
}

/*
 * doupdate --
 *	Make the current screen look like the virtual window "__virtscr".
 */
int
doupdate(void)
{
	WINDOW	*win;
	__LINE	*wlp, *vlp;
	short	 wy;
	int	 dnum, was_cleared, changed;

	/* Check if we need to restart ... */
	if (_cursesi_screen->endwin)
		__restartwin();

	if (_cursesi_screen->curwin)
		win = curscr;
	else
		win = _cursesi_screen->__virtscr;

	/* Initialize loop parameters. */
	_cursesi_screen->ly = curscr->cury;
	_cursesi_screen->lx = curscr->curx;
	wy = 0;

	if (!_cursesi_screen->curwin) {
		for (wy = 0; wy < win->maxy; wy++) {
			wlp = win->alines[wy];
			if (wlp->flags & __ISDIRTY)
				wlp->hash = __hash_line(wlp->line, win->maxx);
		}
	}

	was_cleared = 0;
	if ((win->flags & __CLEAROK) || (curscr->flags & __CLEAROK) ||
	    _cursesi_screen->curwin)
	{
		if (curscr->wattr & __COLOR)
			__unsetattr(0);
		tputs(clear_screen, 0, __cputchar);
		_cursesi_screen->ly = 0;
		_cursesi_screen->lx = 0;
		if (!_cursesi_screen->curwin) {
			curscr->flags &= ~__CLEAROK;
			curscr->cury = 0;
			curscr->curx = 0;
			werase(curscr);
		}
		__touchwin(win, 0);
		win->flags &= ~__CLEAROK;
		/* note we cleared for later */
		was_cleared = 1;
	}
	if (!cursor_address) {
		if (win->curx != 0)
			__cputchar('\n');
		if (!_cursesi_screen->curwin)
			werase(curscr);
	}
	__CTRACE(__CTRACE_REFRESH, "doupdate: (%p): curwin = %d\n", win,
	    _cursesi_screen->curwin);
	__CTRACE(__CTRACE_REFRESH, "doupdate: \tfirstch\tlastch\n");

	if (!_cursesi_screen->curwin) {
		/*
		 * Invoke quickch() only if more than a quarter of the lines
		 * in the window are dirty.
		 */
		for (wy = 0, dnum = 0; wy < win->maxy; wy++)
			if (win->alines[wy]->flags & __ISDIRTY)
				dnum++;
		if (!__noqch && dnum > (int) win->maxy / 4)
			quickch();
	}

#ifdef DEBUG
	{
		int	i, j;

		__CTRACE(__CTRACE_REFRESH,
		    "#####################################\n");
		__CTRACE(__CTRACE_REFRESH,
		    "stdscr(%p)-curscr(%p)-__virtscr(%p)\n",
		    stdscr, curscr, _cursesi_screen->__virtscr);
		for (i = 0; i < curscr->maxy; i++) {
			__CTRACE(__CTRACE_REFRESH, "curscr: %d:", i);
			__CTRACE(__CTRACE_REFRESH, " 0x%x \n",
			    curscr->alines[i]->hash);
			for (j = 0; j < curscr->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, "%c",
				    curscr->alines[i]->line[j].ch);
			__CTRACE(__CTRACE_REFRESH, "\n");
			__CTRACE(__CTRACE_REFRESH, " attr:");
			for (j = 0; j < curscr->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " %x",
				    curscr->alines[i]->line[j].attr);
			__CTRACE(__CTRACE_REFRESH, "\n");
#ifdef HAVE_WCHAR
			__CTRACE(__CTRACE_REFRESH, " wcols:");
			for (j = 0; j < curscr->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " %d",
				    curscr->alines[i]->line[j].wcols);
			__CTRACE(__CTRACE_REFRESH, "\n");

			__CTRACE(__CTRACE_REFRESH, " cflags:");
			for (j = 0; j < curscr->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " 0x%x",
				    curscr->alines[i]->line[j].cflags);
			__CTRACE(__CTRACE_REFRESH, "\n");
#endif /* HAVE_WCHAR */
			__CTRACE(__CTRACE_REFRESH, "win %p: %d:", win, i);
			__CTRACE(__CTRACE_REFRESH, " 0x%x \n",
			    win->alines[i]->hash);
			__CTRACE(__CTRACE_REFRESH, " 0x%x ",
			    win->alines[i]->flags);
			for (j = 0; j < win->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, "%c",
				    win->alines[i]->line[j].ch);
			__CTRACE(__CTRACE_REFRESH, "\n");
			__CTRACE(__CTRACE_REFRESH, " attr:");
			for (j = 0; j < win->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " %x",
				    win->alines[i]->line[j].attr);
			__CTRACE(__CTRACE_REFRESH, "\n");
#ifdef HAVE_WCHAR
			__CTRACE(__CTRACE_REFRESH, " wcols:");
			for (j = 0; j < win->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " %d",
				    win->alines[i]->line[j].wcols);
			__CTRACE(__CTRACE_REFRESH, "\n");
			__CTRACE(__CTRACE_REFRESH, " cflags:");
			for (j = 0; j < win->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " 0x%x",
				    win->alines[i]->line[j].cflags);
			__CTRACE(__CTRACE_REFRESH, "\n");
			__CTRACE(__CTRACE_REFRESH, " nsp:");
			for (j = 0; j < curscr->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " %p",
				    win->alines[i]->line[j].nsp);
			__CTRACE(__CTRACE_REFRESH, "\n");
			__CTRACE(__CTRACE_REFRESH, " bnsp:");
			for (j = 0; j < curscr->maxx; j++)
				__CTRACE(__CTRACE_REFRESH, " %p",
				    win->bnsp);
			__CTRACE(__CTRACE_REFRESH, "\n");
#endif /* HAVE_WCHAR */
		}
	}
#endif /* DEBUG */

	changed = 0;
	for (wy = 0; wy < win->maxy; wy++) {
		wlp = win->alines[wy];
		vlp = _cursesi_screen->__virtscr->alines[win->begy + wy];
/* XXX: remove this */
		__CTRACE(__CTRACE_REFRESH,
		    "doupdate: wy %d\tf: %d\tl:%d\tflags %x\n",
		    wy, *wlp->firstchp, *wlp->lastchp, wlp->flags);
		if (!_cursesi_screen->curwin)
			curscr->alines[wy]->hash = wlp->hash;
		if (wlp->flags & __ISDIRTY || wlp->flags & __ISFORCED) {
			__CTRACE(__CTRACE_REFRESH,
			    "doupdate: [ISDIRTY]wy:%d\tf:%d\tl:%d\n", wy,
			    *wlp->firstchp, *wlp->lastchp);
			/*
			 * We have just cleared so don't force an update
			 * otherwise we spray needless blanks to a cleared
			 * screen.  That is, unless, we are using color,
			 * in this case we need to force the background
			 * color to default.
			 */
			if ((was_cleared == 1) && (__using_color == 0))
				win->alines[wy]->flags &= ~ 0L;
			/*if ((was_cleared == 1) && (__using_color == 0))
				win->alines[wy]->flags &= ~__ISFORCED;*/

			if (makech(wy) == ERR)
				return ERR;
			else {
				if (*wlp->firstchp >= 0)
					*wlp->firstchp = win->maxx;
				if (*wlp->lastchp < win->maxx)
					*wlp->lastchp = win->ch_off;
				if (*wlp->lastchp < *wlp->firstchp) {
					__CTRACE(__CTRACE_REFRESH,
					    "doupdate: line %d notdirty\n", wy);
					wlp->flags &= ~(__ISDIRTY | __ISFORCED);
				}

				/* Check if we have input after
				 * changing N lines. */
				if (_cursesi_screen->checkfd != -1 &&
				    ++changed == CHECK_INTERVAL)
				{
					struct pollfd fds[1];

					/* If we have input, abort. */
					fds[0].fd = _cursesi_screen->checkfd;
					fds[0].events = POLLIN;
					if (poll(fds, 1, 0) > 0)
						goto cleanup;
					changed = 0;
				}
			}
		}

		/*
		 * virtscr is now synced for the line, unset the change
		 * pointers.
		 */
		if (*vlp->firstchp >= 0)
			*vlp->firstchp = _cursesi_screen->__virtscr->maxx;
		if (*vlp->lastchp <= _cursesi_screen->__virtscr->maxx)
			*vlp->lastchp = 0;

		__CTRACE(__CTRACE_REFRESH, "\t%d\t%d\n",
		    *wlp->firstchp, *wlp->lastchp);
	}

	__CTRACE(__CTRACE_REFRESH, "doupdate: ly=%d, lx=%d\n",
	    _cursesi_screen->ly, _cursesi_screen->lx);

	if (_cursesi_screen->curwin)
		domvcur(win, _cursesi_screen->ly, _cursesi_screen->lx,
			win->cury, win->curx);
	else {
		if (win->flags & __LEAVEOK) {
			curscr->cury = _cursesi_screen->ly;
			curscr->curx = _cursesi_screen->lx;
		} else {
			domvcur(win, _cursesi_screen->ly, _cursesi_screen->lx,
				win->cury, win->curx);
			curscr->cury = win->cury;
			curscr->curx = win->curx;
		}
	}

cleanup:
	/* Don't leave the screen with attributes set. */
	__unsetattr(0);
	__do_color_init = 0;
#ifdef DEBUG
#ifdef HAVE_WCHAR
	{
		int	i, j;

		__CTRACE(__CTRACE_REFRESH,
		    "***********after*****************\n");
		__CTRACE(__CTRACE_REFRESH,
		    "stdscr(%p)-curscr(%p)-__virtscr(%p)\n",
		    stdscr, curscr, _cursesi_screen->__virtscr);
		for (i = 0; i < curscr->maxy; i++) {
			for (j = 0; j < curscr->maxx; j++)
				__CTRACE(__CTRACE_REFRESH,
				    "[%d,%d](%x,%x,%d,%x,%p)-(%x,%x,%d,%x,%p)\n",
				    i, j,
				    curscr->alines[i]->line[j].ch,
				    curscr->alines[i]->line[j].attr,
				    curscr->alines[i]->line[j].wcols,
				    curscr->alines[i]->line[j].cflags,
				    curscr->alines[i]->line[j].nsp,
				    _cursesi_screen->__virtscr->alines[i]->line[j].ch,
				    _cursesi_screen->__virtscr->alines[i]->line[j].attr,
				    _cursesi_screen->__virtscr->alines[i]->line[j].wcols,
				    _cursesi_screen->__virtscr->alines[i]->line[j].cflags,
				    _cursesi_screen->__virtscr->alines[i]->line[j].nsp);
		}
	}
#endif /* HAVE_WCHAR */
#endif /* DEBUG */
	return fflush(_cursesi_screen->outfd) == EOF ? ERR : OK;
}

static void
putattr(__LDATA *nsp)
{
	attr_t	off, on;

	__CTRACE(__CTRACE_REFRESH,
	    "putattr: have attr %08x, need attr %08x\n",
	    curscr->wattr
#ifndef HAVE_WCHAR
	    & __ATTRIBUTES
#else
	    & WA_ATTRIBUTES
#endif
	    ,  nsp->attr
#ifndef HAVE_WCHAR
	    & __ATTRIBUTES
#else
	    & WA_ATTRIBUTES
#endif
	    );

	off = (~nsp->attr & curscr->wattr)
#ifndef HAVE_WCHAR
	    & __ATTRIBUTES
#else
	    & WA_ATTRIBUTES
#endif
	    ;

	/*
	 * Unset attributes as appropriate.  Unset first
	 * so that the relevant attributes can be reset
	 * (because 'me' unsets 'mb', 'md', 'mh', 'mk',
	 * 'mp' and 'mr').  Check to see if we also turn off
	 * standout, attributes and colour.
	 */
	if (off & __TERMATTR && exit_attribute_mode != NULL) {
		tputs(exit_attribute_mode, 0, __cputchar);
		curscr->wattr &= __mask_me;
		off &= __mask_me;
	}

	/*
	 * Exit underscore mode if appropriate.
	 * Check to see if we also turn off standout,
	 * attributes and colour.
	 */
	if (off & __UNDERSCORE && exit_underline_mode != NULL) {
		tputs(exit_underline_mode, 0, __cputchar);
		curscr->wattr &= __mask_ue;
		off &= __mask_ue;
	}

	/*
	 * Exit standout mode as appropriate.
	 * Check to see if we also turn off underscore,
	 * attributes and colour.
	 * XXX
	 * Should use uc if so/se not available.
	 */
	if (off & __STANDOUT && exit_standout_mode != NULL) {
		tputs(exit_standout_mode, 0, __cputchar);
		curscr->wattr &= __mask_se;
		off &= __mask_se;
	}

	if (off & __ALTCHARSET && exit_alt_charset_mode != NULL) {
		tputs(exit_alt_charset_mode, 0, __cputchar);
		curscr->wattr &= ~__ALTCHARSET;
	}

	/* Set/change colour as appropriate. */
	if (__using_color)
		__set_color(curscr, nsp->attr & __COLOR);

	on = (nsp->attr & ~curscr->wattr)
#ifndef HAVE_WCHAR
	    & __ATTRIBUTES
#else
	    & WA_ATTRIBUTES
#endif
	    ;

	/*
	 * Enter standout mode if appropriate.
	 */
	if (on & __STANDOUT &&
	    enter_standout_mode != NULL &&
	    exit_standout_mode != NULL)
	{
		tputs(enter_standout_mode, 0, __cputchar);
		curscr->wattr |= __STANDOUT;
	}

	/*
	 * Enter underscore mode if appropriate.
	 * XXX
	 * Should use uc if us/ue not available.
	 */
	if (on & __UNDERSCORE &&
	    enter_underline_mode != NULL &&
	    exit_underline_mode != NULL)
	{
		tputs(enter_underline_mode, 0, __cputchar);
		curscr->wattr |= __UNDERSCORE;
	}

	/*
	 * Set other attributes as appropriate.
	 */
	if (exit_attribute_mode != NULL) {
		if (on & __BLINK && enter_blink_mode != NULL)
		{
			tputs(enter_blink_mode, 0, __cputchar);
			curscr->wattr |= __BLINK;
		}
		if (on & __BOLD && enter_bold_mode != NULL)
		{
			tputs(enter_bold_mode, 0, __cputchar);
			curscr->wattr |= __BOLD;
		}
		if (on & __DIM && enter_dim_mode != NULL)
		{
			tputs(enter_dim_mode, 0, __cputchar);
			curscr->wattr |= __DIM;
		}
		if (on & __BLANK && enter_secure_mode != NULL)
		{
			tputs(enter_secure_mode, 0, __cputchar);
			curscr->wattr |= __BLANK;
		}
		if (on & __PROTECT && enter_protected_mode != NULL)
		{
			tputs(enter_protected_mode, 0, __cputchar);
			curscr->wattr |= __PROTECT;
		}
		if (on & __REVERSE && enter_reverse_mode != NULL)
		{
			tputs(enter_reverse_mode, 0, __cputchar);
			curscr->wattr |= __REVERSE;
		}
#ifdef HAVE_WCHAR
		if (on & WA_TOP && enter_top_hl_mode != NULL)
		{
			tputs(enter_top_hl_mode, 0, __cputchar);
			curscr->wattr |= WA_TOP;
		}
		if (on & WA_LOW && enter_low_hl_mode != NULL)
		{
			tputs(enter_low_hl_mode, 0, __cputchar);
			curscr->wattr |= WA_LOW;
		}
		if (on & WA_LEFT && enter_left_hl_mode != NULL)
		{
			tputs(enter_left_hl_mode, 0, __cputchar);
			curscr->wattr |= WA_LEFT;
		}
		if (on & WA_RIGHT && enter_right_hl_mode != NULL)
		{
			tputs(enter_right_hl_mode, 0, __cputchar);
			curscr->wattr |= WA_RIGHT;
		}
		if (on & WA_HORIZONTAL && enter_horizontal_hl_mode != NULL)
		{
			tputs(enter_horizontal_hl_mode, 0, __cputchar);
			curscr->wattr |= WA_HORIZONTAL;
		}
		if (on & WA_VERTICAL && enter_vertical_hl_mode != NULL)
		{
			tputs(enter_vertical_hl_mode, 0, __cputchar);
			curscr->wattr |= WA_VERTICAL;
		}
#endif /* HAVE_WCHAR */
	}

	/* Enter/exit altcharset mode as appropriate. */
	if (on & __ALTCHARSET && enter_alt_charset_mode != NULL &&
	    exit_alt_charset_mode != NULL) {
		tputs(enter_alt_charset_mode, 0, __cputchar);
		curscr->wattr |= __ALTCHARSET;
	}
}

static void
putattr_out(__LDATA *nsp)
{

	if (underline_char &&
	    ((nsp->attr & __STANDOUT) || (nsp->attr & __UNDERSCORE)))
	{
		__cputchar('\b');
		tputs(underline_char, 0, __cputchar);
	}
}

static int
putch(__LDATA *nsp, __LDATA *csp, int wy, int wx)
{
#ifdef HAVE_WCHAR
	int i;
	__LDATA *tcsp;
#endif /* HAVE_WCHAR */

	if (csp != NULL)
		putattr(nsp);

	if (!_cursesi_screen->curwin && csp) {
		csp->attr = nsp->attr;
		csp->ch = nsp->ch;
		csp->cflags = nsp->cflags;
#ifdef HAVE_WCHAR
		if (_cursesi_copy_nsp(nsp->nsp, csp) == ERR)
			return ERR;
		csp->wcols = nsp->wcols;

		if (nsp->wcols > 1) {
			tcsp = csp;
			tcsp++;
			for (i = nsp->wcols - 1; i > 0; i--) {
				tcsp->ch = csp->ch;
				tcsp->attr = csp->attr;
				tcsp->wcols = i;
				tcsp->cflags = CA_CONTINUATION;
				tcsp++;
			}
		}
#endif /* HAVE_WCHAR */
	}

#ifndef HAVE_WCHAR
	__cputchar((int)nsp->ch);
#else
	if ((nsp->wcols <= 0) || (nsp->cflags & CA_CONTINUATION))
		goto out;

	if (((_cursesi_screen->nca & nsp->attr) == 0) && (__using_color == 1) &&
	    csp == NULL)
		__set_color(curscr, nsp->attr & __COLOR);
	__cputwchar((int)nsp->ch);
	__CTRACE(__CTRACE_REFRESH,
	    "putch: (%d,%d)putwchar(0x%x)\n", wy, wx, nsp->ch);

	/* Output non-spacing characters for the cell. */
	__cursesi_putnsp(nsp->nsp, wy, wx);
out:
#endif /* HAVE_WCHAR */

	if (csp != NULL)
		putattr_out(nsp);
	return OK;
}

static int
putchbr(__LDATA *nsp, __LDATA *csp, __LDATA *psp, int wy, int wx)
{
	int error, cw, pcw;

	/* Can safely print to bottom right corner. */
	if (!auto_right_margin)
		return putch(nsp, csp, wy, wx);

	/* Disable auto margins temporarily. */
	if (enter_am_mode && exit_am_mode) {
		tputs(exit_am_mode, 0, __cputchar);
		error = putch(nsp, csp, wy, wx);
		tputs(enter_am_mode, 0, __cputchar);
		return error;
	}

	/* We need to insert characters. */
#ifdef HAVE_WCHAR
	cw = nsp->wcols;
	pcw = psp->wcols;
	if (cw < 1 || pcw < 1)
		return ERR; /* Nothing to insert */

	/* When inserting a wide character, we need something other than
	 * insert_character. */
	if (pcw > 1 &&
	    !(parm_ich != NULL ||
	    (enter_insert_mode != NULL && exit_insert_mode != NULL)))
		return ERR;
#else
	cw = pcw = 1;
#endif /* HAVE_WCHAR */

	/* Write the corner character at wx - pcw. */
	__mvcur(wy, wx, wy, wx - pcw, 1);
	if (putch(nsp, csp, wy, wx) == ERR)
		return ERR;

	/* Move cursor back. */
	__mvcur(wy, wx - pcw + cw, wy, wx - cw, 1);

	putattr(psp);

	/* Enter insert mode. */
	if (pcw == 1 && insert_character != NULL)
		tputs(insert_character, 0, __cputchar);
	else if (parm_ich != NULL)
		tputs(tiparm(parm_ich, (long)pcw), 0, __cputchar);
	else if (enter_insert_mode != NULL && exit_insert_mode != NULL)
		tputs(enter_insert_mode, 0, __cputchar);
	else
		return ERR;

	/* Insert the old character back. */
	error = putch(psp, NULL, wy, wx - pcw);

	/* Exit insert mode. */
	if (insert_character != NULL || parm_ich != NULL)
		;
	else if (enter_insert_mode != NULL && exit_insert_mode != NULL)
		tputs(exit_insert_mode, 0, __cputchar);

	putattr_out(psp);

	return error;
}

/*
 * makech --
 *	Make a change on the screen.
 */
static int
makech(int wy)
{
	WINDOW	*win;
	static __LDATA blank;
	__LDATA *nsp, *csp, *cp, *cep, *fsp;
	__LINE *wlp;
	int	nlsp;	/* offset to first space at eol. */
	size_t	mlsp;
	int	lch, wx, owx, chw;
	const char	*ce;
	attr_t	lspc;		/* Last space colour */
	attr_t	battr;		/* background attribute bits */
	attr_t	attr_mask;	/* attributes mask */

#ifdef __GNUC__
	nlsp = lspc = 0;	/* XXX gcc -Wuninitialized */
#endif
	if (_cursesi_screen->curwin)
		win = curscr;
	else
		win = __virtscr;

	blank.ch = win->bch;
	blank.attr = win->battr;
	blank.cflags = CA_BACKGROUND;
#ifdef HAVE_WCHAR
	if (_cursesi_copy_nsp(win->bnsp, &blank) == ERR)
		return ERR;
	blank.wcols = win->wcols;
	attr_mask = WA_ATTRIBUTES;
#else
	attr_mask = A_ATTRIBUTES;
#endif /* HAVE_WCHAR */
	battr = win->battr & attr_mask;

#ifdef DEBUG
#if HAVE_WCHAR
	{
		int x;
		__LDATA *lp, *vlp;

		__CTRACE(__CTRACE_REFRESH,
		    "[makech-before]wy=%d,curscr(%p)-__virtscr(%p)\n",
		    wy, curscr, __virtscr);
		for (x = 0; x < curscr->maxx; x++) {
			lp = &curscr->alines[wy]->line[x];
			vlp = &__virtscr->alines[wy]->line[x];
			__CTRACE(__CTRACE_REFRESH,
			    "[%d,%d](%x,%x,%d,%x,%x,%d,%p)-"
			    "(%x,%x,%d,%x,%x,%d,%p)\n",
			    wy, x, lp->ch, lp->attr, lp->wcols,
			    win->bch, win->battr, win->wcols, lp->nsp,
			    vlp->ch, vlp->attr, vlp->wcols,
			    win->bch, win->battr, win->wcols, vlp->nsp);
		}
	}
#endif /* HAVE_WCHAR */
#endif /* DEBUG */

	/* Is the cursor still on the end of the last line? */
	if (wy > 0 && curscr->alines[wy - 1]->flags & __ISPASTEOL) {
		domvcur(win, _cursesi_screen->ly, _cursesi_screen->lx,
			_cursesi_screen->ly + 1, 0);
		_cursesi_screen->ly++;
		_cursesi_screen->lx = 0;
	}
	wlp = win->alines[wy];
	wx = *win->alines[wy]->firstchp;
	if (wx < 0)
		wx = 0;
	else
		if (wx >= win->maxx)
			return (OK);
	lch = *win->alines[wy]->lastchp;
	if (lch < 0)
		return OK;
	else
		if (lch >= (int) win->maxx)
			lch = win->maxx - 1;

	if (_cursesi_screen->curwin) {
		csp = &blank;
		__CTRACE(__CTRACE_REFRESH, "makech: csp is blank\n");
	} else {
		csp = &curscr->alines[wy]->line[wx];
		__CTRACE(__CTRACE_REFRESH,
		    "makech: csp is on curscr:(%d,%d)\n", wy, wx);
	}


	while (win->alines[wy]->line[wx].cflags & CA_CONTINUATION) {
		wx--;
		if (wx <= 0) {
			wx = 0;
			break;
		}
	}

	nsp = fsp = &win->alines[wy]->line[wx];

#ifdef DEBUG
	if (_cursesi_screen->curwin)
		__CTRACE(__CTRACE_REFRESH,
		    "makech: nsp is at curscr:(%d,%d)\n", wy, wx);
	else
		__CTRACE(__CTRACE_REFRESH,
		    "makech: nsp is at __virtscr:(%d,%d)\n", wy, wx);
#endif /* DEBUG */

	/*
	 * Work out if we can use a clear to end of line.  If we are
	 * using color then we can only erase the line if the terminal
	 * can erase to the background color.
	 */
	if (clr_eol && !_cursesi_screen->curwin && (!(__using_color)
	    || (__using_color && back_color_erase))) {
		nlsp = win->maxx - 1;
		cp = &win->alines[wy]->line[win->maxx - 1];
#ifdef HAVE_WCHAR
		while ((_cursesi_celleq(cp, &blank) == 1) &&
#else
		while (cp->ch == blank.ch &&
#endif /* HAVE_WCHAR */
		    ((cp->attr & attr_mask) == battr)) {
#ifdef HAVE_WCHAR
			nlsp -= cp->wcols;
			cp -= cp->wcols;
#else
			nlsp--;
			cp--;
#endif /* HAVE_WCHAR */

			if (nlsp <= 0)
				break;
		}


		if (nlsp < 0)
			nlsp = 0;
	}

	ce = clr_eol;

	while (wx <= lch) {
		__CTRACE(__CTRACE_REFRESH, "makech: wx=%d,lch=%d, nlsp=%d\n", wx, lch, nlsp);
#ifdef HAVE_WCHAR
		__CTRACE(__CTRACE_REFRESH, "makech: farnarkle: flags 0x%x, cflags 0x%x, color_init %d, celleq %d\n",
			wlp->flags, nsp->cflags, __do_color_init, _cursesi_celleq(nsp, csp));
		__CTRACE(__CTRACE_REFRESH, "makech: nsp=(%x,%x,%d,%x,%x,%d,%p)\n",
			nsp->ch, nsp->attr, nsp->wcols, win->bch, win->battr,
			win->wcols, nsp->nsp);
		__CTRACE(__CTRACE_REFRESH, "makech: csp=(%x,%x,%d,%x,%x,%d,%p)\n",
			csp->ch, csp->attr, csp->wcols, win->bch, win->battr,
			win->wcols, csp->nsp);
#endif
		if (!(wlp->flags & __ISFORCED) &&
#ifdef HAVE_WCHAR
		    ((nsp->cflags & CA_CONTINUATION) != CA_CONTINUATION) &&
#endif
		    _cursesi_celleq(nsp, csp))
		{
			if (wx <= lch) {
				while (wx <= lch && _cursesi_celleq(nsp, csp)) {
#ifdef HAVE_WCHAR
					wx += nsp->wcols;
					if (!_cursesi_screen->curwin)
						csp += nsp->wcols;
					nsp += nsp->wcols;
#else
					wx++;
					nsp++;
					if (!_cursesi_screen->curwin)
						++csp;
#endif
				}
				continue;
			}
			break;
		}

		domvcur(win, _cursesi_screen->ly, _cursesi_screen->lx, wy, wx);

		__CTRACE(__CTRACE_REFRESH, "makech: 1: wx = %d, ly= %d, "
		    "lx = %d, newy = %d, newx = %d, lch = %d\n",
		    wx, _cursesi_screen->ly, _cursesi_screen->lx, wy, wx, lch);
		_cursesi_screen->ly = wy;
		_cursesi_screen->lx = wx;
		owx = wx;
		while (wx <= lch &&
		       ((wlp->flags & __ISFORCED) || !_cursesi_celleq(nsp, csp)))
		{
			if ((ce != NULL) && (wx >= nlsp) &&
			    (nsp->ch == blank.ch) &&
			    (__do_color_init == 1 || nsp->attr == blank.attr))
			{
				/* Check for clear to end-of-line. */
				cep = &win->alines[wy]->line[win->maxx - 1];
				while (cep->ch == blank.ch && cep->attr == battr)
					if (cep-- <= csp)
						break;

				mlsp = &win->alines[wy]->line[win->maxx - 1]
				    - win->alines[wy]->line
				    - win->begx * __LDATASIZE;

				__CTRACE(__CTRACE_REFRESH,
				    "makech: nlsp = %d, max = %zu, strlen(ce) = %zu\n",
				    nlsp, mlsp, strlen(ce));
				__CTRACE(__CTRACE_REFRESH,
				    "makech: line = %p, cep = %p, begx = %u\n",
				    win->alines[wy]->line, cep, win->begx);

				/*
				 * work out how to clear the line.  If:
				 *  - clear len is greater than clear_to_eol len
				 *  - background char == ' '
				 *  - we are not at EOL
				 *  - using color and term can erase to
				 *    background color
				 *  - if we are at the bottom of the window
				 *    (to prevent a scroll)
				 * then emit the ce string.
				 */
				if (((wy == win->maxy - 1) ||
				    ((mlsp - wx) > strlen(ce))) &&
				     ((__using_color && back_color_erase) ||
				      (! __using_color))) {
					if (wlp->line[wx].attr & win->screen->nca) {
						__unsetattr(0);
					} else if (__using_color &&
					    ((__do_color_init == 1) ||
					    ((lspc & __COLOR) !=
					    (curscr->wattr & __COLOR)))) {
						__set_color(curscr, lspc &
						    __COLOR);
					}
					tputs(ce, 0, __cputchar);
					_cursesi_screen->lx = wx + win->begx;
					csp = &curscr->alines[wy]->line[wx + win->begx];
					wx = wx + win->begx;
					while (wx++ <= (curscr->maxx - 1)) {
						csp->attr = blank.attr;
						csp->ch = blank.ch;
						csp->cflags = CA_BACKGROUND;
#ifdef HAVE_WCHAR
						if (_cursesi_copy_nsp(blank.nsp, csp) == ERR)
							return ERR;
						csp->wcols = blank.wcols;
						csp += blank.wcols;
#else
						csp++;
#endif /* HAVE_WCHAR */
						assert(csp != &blank);
					}
					return OK;
				}
			}

#ifdef HAVE_WCHAR
			chw = nsp->wcols;
			if (chw < 0)
				chw = 0; /* match putch() */
#else
			chw = 1;
#endif /* HAVE_WCHAR */
			owx = wx;
			if (wx + chw >= (win->maxx) &&
			    wy == win->maxy - 1 && !_cursesi_screen->curwin)
			{
				if (win->flags & __ENDLINE)
					__unsetattr(1);
				if (!(win->flags & __SCROLLWIN)) {
					int e;

					if (win->flags & __SCROLLOK)
						e = putch(nsp, csp, wy, wx);
					else {
						e = putchbr(nsp, csp,
						    nsp == fsp ? NULL : nsp - 1,
						    wy, wx);
					}
					if (e == ERR)
						return ERR;
				}
				if (wx + chw < curscr->maxx) {
					domvcur(win,
					    _cursesi_screen->ly, wx,
					    (int)(win->maxy - 1),
					    (int)(win->maxx - 1));
				}
				_cursesi_screen->ly = win->maxy - 1;
				_cursesi_screen->lx = win->maxx - 1;
				return OK;
			}
			if (wx + chw < win->maxx || wy < win->maxy - 1 ||
			    !(win->flags & __SCROLLWIN))
			{
				if (putch(nsp, csp, wy, wx) == ERR)
					return ERR;
			} else {
				putattr(nsp);
				putattr_out(nsp);
			}
			wx += chw;
			nsp += chw;
			if (!_cursesi_screen->curwin)
				csp += chw;

			__CTRACE(__CTRACE_REFRESH,
			    "makech: 2: wx = %d, lx = %d\n",
			    wx, _cursesi_screen->lx);
		}
		if (_cursesi_screen->lx == wx)	/* If no change. */
			break;

		/*
		 * We need to work out if the cursor has been put in the
		 * middle of a wide character so check if curx is between
		 * where we were and where we are and we are on the right
		 * line.  If so, move the cursor now.
		 */
		if ((wy == win->cury) && (wx > win->curx) &&
		    (owx < win->curx)) {
			_cursesi_screen->lx = win->curx;
			domvcur(win, _cursesi_screen->ly, wx,
			    _cursesi_screen->ly, _cursesi_screen->lx);
		} else
			_cursesi_screen->lx = wx;

		if (_cursesi_screen->lx >= COLS && auto_right_margin)
			_cursesi_screen->lx = COLS - 1;
		else
			if (wx >= win->maxx) {
				domvcur(win,
					_cursesi_screen->ly,
					_cursesi_screen->lx,
					_cursesi_screen->ly,
					(int)(win->maxx - 1));
				_cursesi_screen->lx = win->maxx - 1;
			}
		__CTRACE(__CTRACE_REFRESH, "makech: 3: wx = %d, lx = %d\n",
		    wx, _cursesi_screen->lx);
	}
#ifdef DEBUG
#if HAVE_WCHAR
	{
		int x;
		__LDATA *lp, *vlp;

		__CTRACE(__CTRACE_REFRESH,
		    "makech-after: curscr(%p)-__virtscr(%p)\n",
		    curscr, __virtscr );
		for (x = 0; x < curscr->maxx; x++) {
			lp = &curscr->alines[wy]->line[x];
			vlp = &__virtscr->alines[wy]->line[x];
			__CTRACE(__CTRACE_REFRESH,
			    "[%d,%d](%x,%x,%d,%x,%x,%d,%p)-"
			    "(%x,%x,%d,%x,%x,%d,%p)\n",
			    wy, x, lp->ch, lp->attr, lp->wcols,
			    win->bch, win->battr, win->wcols, lp->nsp,
			    vlp->ch, vlp->attr, vlp->wcols,
			    win->bch, win->battr, win->wcols, vlp->nsp);
		}
	}
#endif /* HAVE_WCHAR */
#endif /* DEBUG */

	return OK;
}

/*
 * domvcur --
 *	Do a mvcur, leaving attributes if necessary.
 */
static void
domvcur(WINDOW *win, int oy, int ox, int ny, int nx)
{

	__CTRACE(__CTRACE_REFRESH, "domvcur: (%d,%d)=>(%d,%d) win %p\n",
	    oy, ox, ny, nx, win );

	__unsetattr(1);

	/* Don't move the cursor unless we need to. */
	if (oy == ny && ox == nx) {
		/* Check EOL. */
		if (!(win->alines[oy]->flags & __ISPASTEOL))
			return;
	}

	/* Clear EOL flags. */
	win->alines[oy]->flags &= ~__ISPASTEOL;
	win->alines[ny]->flags &= ~__ISPASTEOL;

	__mvcur(oy, ox, ny, nx, 1);
}

/*
 * Quickch() attempts to detect a pattern in the change of the window
 * in order to optimize the change, e.g., scroll n lines as opposed to
 * repainting the screen line by line.
 */

static __LDATA buf[128];
static  unsigned int last_hash;
static  size_t last_hash_len;
#define BLANKSIZE (sizeof(buf) / sizeof(buf[0]))

static void
quickch(void)
{
#define THRESH		(int) __virtscr->maxy / 4

	__LINE *clp, *tmp1, *tmp2;
	int	bsize, curs, curw, starts, startw, i, j;
	int	n, target, cur_period, bot, top, sc_region;
	unsigned int	blank_hash, found;
	attr_t	bcolor;

#ifdef __GNUC__
	curs = curw = starts = startw = 0;	/* XXX gcc -Wuninitialized */
#endif
	/*
	 * Find how many lines from the top of the screen are unchanged.
	 */
	for (top = 0; top < __virtscr->maxy; top++) {
		if (__virtscr->alines[top]->flags & __ISDIRTY &&
		    (__virtscr->alines[top]->hash != curscr->alines[top]->hash ||
		     !lineeq(__virtscr->alines[top]->line,
			     curscr->alines[top]->line,
			     (size_t) __virtscr->maxx))) {
			break;
		} else
			__virtscr->alines[top]->flags &= ~__ISDIRTY;
	}
	/*
	 * Find how many lines from bottom of screen are unchanged.
	 */
	for (bot = __virtscr->maxy - 1; bot >= 0; bot--) {
		if (__virtscr->alines[bot]->flags & __ISDIRTY &&
		    (__virtscr->alines[bot]->hash != curscr->alines[bot]->hash ||
		     !lineeq(__virtscr->alines[bot]->line,
			     curscr->alines[bot]->line,
			     (size_t) __virtscr->maxx))) {
			break;
		} else
			__virtscr->alines[bot]->flags &= ~__ISDIRTY;
	}

	/*
	 * Work round an xterm bug where inserting lines causes all the
	 * inserted lines to be covered with the background colour we
	 * set on the first line (even if we unset it for subsequent
	 * lines).
	 */
	bcolor = __virtscr->alines[min(top,
	    __virtscr->maxy - 1)]->line[0].attr & __COLOR;
	for (i = top + 1, j = 0; i < bot; i++) {
		if ((__virtscr->alines[i]->line[0].attr & __COLOR) != bcolor) {
			bcolor = __virtscr->alines[i]->line[__virtscr->maxx].
			    attr & __COLOR;
			j = i - top;
		} else
			break;
	}
	top += j;

#ifdef NO_JERKINESS
	/*
	 * If we have a bottom unchanged region return.  Scrolling the
	 * bottom region up and then back down causes a screen jitter.
	 * This will increase the number of characters sent to the screen
	 * but it looks better.
	 */
	if (bot < __virtscr->maxy - 1)
		return;
#endif				/* NO_JERKINESS */

	/*
	 * Search for the largest block of text not changed.
	 * Invariants of the loop:
	 * - Startw is the index of the beginning of the examined block in
	 *   __virtscr.
	 * - Starts is the index of the beginning of the examined block in
	 *   curscr.
	 * - Curw is the index of one past the end of the exmined block in
	 *   __virtscr.
	 * - Curs is the index of one past the end of the exmined block in
	 *   curscr.
	 * - bsize is the current size of the examined block.
	*/

	found = 0;
	for (bsize = bot - top; bsize >= THRESH; bsize--) {
		for (startw = top; startw <= bot - bsize; startw++)
			for (starts = top; starts <= bot - bsize; starts++) {
/*				for (curw = startw, curs = starts;
				    curs < starts + bsize; curw++, curs++)
					if (__virtscr->alines[curw]->hash !=
					    curscr->alines[curs]->hash)
						break;
				if (curs != starts + bsize)
					continue;*/
				for (curw = startw, curs = starts;
					curs < starts + bsize; curw++, curs++)
					if (!lineeq(__virtscr->alines[curw]->line,
						    curscr->alines[curs]->line,
						    (size_t) __virtscr->maxx)) {
						found = 1;
						break;
				}
				if ((curs == starts + bsize) && (found == 1)) {
					goto done;
				}
			}
	}
done:

	__CTRACE(__CTRACE_REFRESH, "quickch:bsize=%d, THRESH=%d, starts=%d, "
	    "startw=%d, curw=%d, curs=%d, top=%d, bot=%d\n",
	    bsize, THRESH, starts, startw, curw, curs, top, bot);

	/* Did not find anything */
	if (bsize < THRESH)
		return;

	/*
	 * Make sure that there is no overlap between the bottom and top
	 * regions and the middle scrolled block.
	 */
	if (bot < curs)
		bot = curs - 1;
	if (top > starts)
		top = starts;

	n = startw - starts;

#ifdef DEBUG
	__CTRACE(__CTRACE_REFRESH, "#####################################\n");
	__CTRACE(__CTRACE_REFRESH, "quickch: n = %d\n", n);
	for (i = 0; i < curscr->maxy; i++) {
		__CTRACE(__CTRACE_REFRESH, "C: %d:", i);
		__CTRACE(__CTRACE_REFRESH, " 0x%x \n", curscr->alines[i]->hash);
		for (j = 0; j < curscr->maxx; j++)
			__CTRACE(__CTRACE_REFRESH, "%c",
			    curscr->alines[i]->line[j].ch);
		__CTRACE(__CTRACE_REFRESH, "\n");
		__CTRACE(__CTRACE_REFRESH, " attr:");
		for (j = 0; j < curscr->maxx; j++)
			__CTRACE(__CTRACE_REFRESH, " %x",
			    curscr->alines[i]->line[j].attr);
		__CTRACE(__CTRACE_REFRESH, "\n");
		__CTRACE(__CTRACE_REFRESH, "W: %d:", i);
		__CTRACE(__CTRACE_REFRESH, " 0x%x \n",
		    __virtscr->alines[i]->hash);
		__CTRACE(__CTRACE_REFRESH, " 0x%x ",
		    __virtscr->alines[i]->flags);
		for (j = 0; j < __virtscr->maxx; j++)
			__CTRACE(__CTRACE_REFRESH, "%c",
			    __virtscr->alines[i]->line[j].ch);
		__CTRACE(__CTRACE_REFRESH, "\n");
		__CTRACE(__CTRACE_REFRESH, " attr:");
		for (j = 0; j < __virtscr->maxx; j++)
			__CTRACE(__CTRACE_REFRESH, " %x",
			    __virtscr->alines[i]->line[j].attr);
		__CTRACE(__CTRACE_REFRESH, "\n");
	}
#endif

#ifndef HAVE_WCHAR
	if (buf[0].ch != curscr->bch) {
		for (i = 0; i < BLANKSIZE; i++) {
			buf[i].ch = curscr->bch;
			buf[i].attr = 0;
			buf[i].cflags = CA_BACKGROUND;
		}
	}
#else
	if (buf[0].ch != curscr->bch) {
		for (i = 0; i < BLANKSIZE; i++) { /* XXXX: BLANKSIZE may not be valid if wcols > 1 */
			buf[i].ch = curscr->bch;
			if (_cursesi_copy_nsp(curscr->bnsp, &buf[i]) == ERR)
				return;
			buf[i].attr = 0;
			buf[i].cflags = CA_BACKGROUND;
			buf[i].wcols = curscr->wcols;
		}
	}
#endif /* HAVE_WCHAR */

	if (__virtscr->maxx != last_hash_len) {
		blank_hash = 0;
		for (i = __virtscr->maxx; i > BLANKSIZE; i -= BLANKSIZE) {
			blank_hash = __hash_more(buf, sizeof(buf), blank_hash);
		}
		blank_hash = __hash_more((char *)(void *)buf,
		    i * sizeof(buf[0]), blank_hash);
		/* cache result in static data - screen width doesn't change often */
		last_hash_len = __virtscr->maxx;
		last_hash = blank_hash;
	} else
		blank_hash = last_hash;

	/*
	 * Perform the rotation to maintain the consistency of curscr.
	 * This is hairy since we are doing an *in place* rotation.
	 * Invariants of the loop:
	 * - I is the index of the current line.
	 * - Target is the index of the target of line i.
	 * - Tmp1 points to current line (i).
	 * - Tmp2 and points to target line (target);
	 * - Cur_period is the index of the end of the current period.
	 *   (see below).
	 *
	 * There are 2 major issues here that make this rotation non-trivial:
	 * 1.  Scrolling in a scrolling region bounded by the top
	 *     and bottom regions determined (whose size is sc_region).
	 * 2.  As a result of the use of the mod function, there may be a
	 *     period introduced, i.e., 2 maps to 4, 4 to 6, n-2 to 0, and
	 *     0 to 2, which then causes all odd lines not to be rotated.
	 *     To remedy this, an index of the end ( = beginning) of the
	 *     current 'period' is kept, cur_period, and when it is reached,
	 *     the next period is started from cur_period + 1 which is
	 *     guaranteed not to have been reached since that would mean that
	 *     all records would have been reached. (think about it...).
	 *
	 * Lines in the rotation can have 3 attributes which are marked on the
	 * line so that curscr is consistent with the visual screen.
	 * 1.  Not dirty -- lines inside the scrolled block, top region or
	 *                  bottom region.
	 * 2.  Blank lines -- lines in the differential of the scrolling
	 *		      region adjacent to top and bot regions
	 *                    depending on scrolling direction.
	 * 3.  Dirty line -- all other lines are marked dirty.
	 */
	sc_region = bot - top + 1;
	i = top;
	tmp1 = curscr->alines[top];
	cur_period = top;
	for (j = top; j <= bot; j++) {
		target = (i - top + n + sc_region) % sc_region + top;
		tmp2 = curscr->alines[target];
		curscr->alines[target] = tmp1;
		/* Mark block as clean and blank out scrolled lines. */
		clp = curscr->alines[target];
		__CTRACE(__CTRACE_REFRESH,
		    "quickch: n=%d startw=%d curw=%d i = %d target=%d ",
		    n, startw, curw, i, target);
		if ((target >= startw && target < curw) || target < top
		    || target > bot)
		{
			__CTRACE(__CTRACE_REFRESH, " notdirty\n");
			__virtscr->alines[target]->flags &= ~__ISDIRTY;
		} else
			if ((n > 0 && target >= top && target < top + n) ||
			    (n < 0 && target <= bot && target > bot + n))
			{
				if (clp->hash != blank_hash ||
				    !lineeq(clp->line, clp->line + 1,
					    (__virtscr->maxx - 1)) ||
				    !_cursesi_celleq(clp->line, buf))
				{
					for (i = __virtscr->maxx;
					    i > BLANKSIZE;
					    i -= BLANKSIZE) {
						(void) memcpy(clp->line + i -
						    BLANKSIZE, buf, sizeof(buf));
					}
					(void)memcpy(clp->line, buf,
					    i * sizeof(buf[0]));
					__CTRACE(__CTRACE_REFRESH,
					    " blanked out: dirty\n");
					clp->hash = blank_hash;
					__touchline(__virtscr, target, 0, (int) __virtscr->maxx - 1);
				} else {
					__CTRACE(__CTRACE_REFRESH,
					    " -- blank line already: dirty\n");
					__touchline(__virtscr, target, 0, (int) __virtscr->maxx - 1);
				}
			} else {
				__CTRACE(__CTRACE_REFRESH, " -- dirty\n");
				__touchline(__virtscr, target, 0,
				    (int)__virtscr->maxx - 1);
			}
		if (target == cur_period) {
			i = target + 1;
			tmp1 = curscr->alines[i];
			cur_period = i;
		} else {
			tmp1 = tmp2;
			i = target;
		}
	}
#ifdef DEBUG
	__CTRACE(__CTRACE_REFRESH, "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
	for (i = 0; i < curscr->maxy; i++) {
		__CTRACE(__CTRACE_REFRESH, "C: %d:", i);
		for (j = 0; j < curscr->maxx; j++)
			__CTRACE(__CTRACE_REFRESH, "%c",
			    curscr->alines[i]->line[j].ch);
		__CTRACE(__CTRACE_REFRESH, "\n");
		__CTRACE(__CTRACE_REFRESH, "W: %d:", i);
		for (j = 0; j < __virtscr->maxx; j++)
			__CTRACE(__CTRACE_REFRESH, "%c",
			    __virtscr->alines[i]->line[j].ch);
		__CTRACE(__CTRACE_REFRESH, "\n");
	}
#endif
	if (n != 0)
		scrolln(starts, startw, curs, bot, top);
}

/*
 * scrolln --
 *	Scroll n lines, where n is starts - startw.
 */
static void /* ARGSUSED */
scrolln(int starts, int startw, int curs, int bot, int top)
{
	int	i, oy, ox, n;

	oy = curscr->cury;
	ox = curscr->curx;
	n = starts - startw;

	/*
	 * XXX
	 * The initial tests that set __noqch don't let us reach here unless
	 * we have either cs + ho + SF/sf/SR/sr, or AL + DL.  SF/sf and SR/sr
	 * scrolling can only shift the entire scrolling region, not just a
	 * part of it, which means that the quickch() routine is going to be
	 * sadly disappointed in us if we don't have cs as well.
	 *
	 * If cs, ho and SF/sf are set, can use the scrolling region.  Because
	 * the cursor position after cs is undefined, we need ho which gives us
	 * the ability to move to somewhere without knowledge of the current
	 * location of the cursor.  Still call __mvcur() anyway, to update its
	 * idea of where the cursor is.
	 *
	 * When the scrolling region has been set, the cursor has to be at the
	 * last line of the region to make the scroll happen.
	 *
	 * Doing SF/SR or AL/DL appears faster on the screen than either sf/sr
	 * or AL/DL, and, some terminals have AL/DL, sf/sr, and cs, but not
	 * SF/SR.  So, if we're scrolling almost all of the screen, try and use
	 * AL/DL, otherwise use the scrolling region.  The "almost all" is a
	 * shameless hack for vi.
	 */
	if (n > 0) {
		if (change_scroll_region != NULL && cursor_home != NULL &&
		    (parm_index != NULL ||
		    ((parm_insert_line == NULL || parm_delete_line == NULL ||
		    top > 3 || bot + 3 < __virtscr->maxy) &&
		    scroll_forward != NULL)))
		{
			tputs(tiparm(change_scroll_region, top, bot),
			    0, __cputchar);
			__mvcur(oy, ox, 0, 0, 1);
			tputs(cursor_home, 0, __cputchar);
			__mvcur(0, 0, bot, 0, 1);
			if (parm_index != NULL)
				tputs(tiparm(parm_index, n),
				    0, __cputchar);
			else
				for (i = 0; i < n; i++)
					tputs(scroll_forward, 0, __cputchar);
			tputs(tiparm(change_scroll_region,
			    0, (int)__virtscr->maxy - 1), 0, __cputchar);
			__mvcur(bot, 0, 0, 0, 1);
			tputs(cursor_home, 0, __cputchar);
			__mvcur(0, 0, oy, ox, 1);
			return;
		}

		/* Scroll up the block. */
		if (parm_index != NULL && top == 0) {
			__mvcur(oy, ox, bot, 0, 1);
			tputs(tiparm(parm_index, n), 0, __cputchar);
		} else
			if (parm_delete_line != NULL) {
				__mvcur(oy, ox, top, 0, 1);
				tputs(tiparm(parm_delete_line, n),
				    0, __cputchar);
			} else
				if (delete_line != NULL) {
					__mvcur(oy, ox, top, 0, 1);
					for (i = 0; i < n; i++)
						tputs(delete_line, 0,
						    __cputchar);
				} else
					if (scroll_forward != NULL && top == 0) {
						__mvcur(oy, ox, bot, 0, 1);
						for (i = 0; i < n; i++)
							tputs(scroll_forward, 0,
							    __cputchar);
					} else
						abort();

		/* Push down the bottom region. */
		__mvcur(top, 0, bot - n + 1, 0, 1);
		if (parm_insert_line != NULL)
			tputs(tiparm(parm_insert_line, n), 0, __cputchar);
		else {
			if (insert_line != NULL) {
				for (i = 0; i < n; i++)
					tputs(insert_line, 0, __cputchar);
			} else
				abort();
		}
		__mvcur(bot - n + 1, 0, oy, ox, 1);
	} else {
		/*
		 * !!!
		 * n < 0
		 *
		 * If cs, ho and SR/sr are set, can use the scrolling region.
		 * See the above comments for details.
		 */
		if (change_scroll_region != NULL && cursor_home != NULL &&
		    (parm_rindex != NULL ||
		    ((parm_insert_line == NULL || parm_delete_line == NULL ||
		    top > 3 ||
		    bot + 3 < __virtscr->maxy) && scroll_reverse != NULL)))
		{
			tputs(tiparm(change_scroll_region, top, bot),
			    0, __cputchar);
			__mvcur(oy, ox, 0, 0, 1);
			tputs(cursor_home, 0, __cputchar);
			__mvcur(0, 0, top, 0, 1);

			if (parm_rindex != NULL)
				tputs(tiparm(parm_rindex, -n),
				    0, __cputchar);
			else
				for (i = n; i < 0; i++)
					tputs(scroll_reverse, 0, __cputchar);
			tputs(tiparm(change_scroll_region,
			    0, (int) __virtscr->maxy - 1), 0, __cputchar);
			__mvcur(top, 0, 0, 0, 1);
			tputs(cursor_home, 0, __cputchar);
			__mvcur(0, 0, oy, ox, 1);
			return;
		}

		/* Preserve the bottom lines. */
		__mvcur(oy, ox, bot + n + 1, 0, 1);
		if (parm_rindex != NULL && bot == __virtscr->maxy)
			tputs(tiparm(parm_rindex, -n), 0, __cputchar);
		else {
			if (parm_delete_line != NULL)
				tputs(tiparm(parm_delete_line, -n),
				    0, __cputchar);
			else {
				if (delete_line != NULL)
					for (i = n; i < 0; i++)
						tputs(delete_line,
						    0, __cputchar);
				else {
					if (scroll_reverse != NULL &&
					    bot == __virtscr->maxy)
						for (i = n; i < 0; i++)
							tputs(scroll_reverse, 0,
							    __cputchar);
					else
						abort();
				}
			}
		}
		/* Scroll the block down. */
		__mvcur(bot + n + 1, 0, top, 0, 1);
		if (parm_insert_line != NULL)
			tputs(tiparm(parm_insert_line, -n), 0, __cputchar);
		else
			if (insert_line != NULL)
				for (i = n; i < 0; i++)
					tputs(insert_line, 0, __cputchar);
			else
				abort();
		__mvcur(top, 0, oy, ox, 1);
	}
}

/*
 * __unsetattr --
 *	Unset attributes on curscr.  Leave standout, attribute and colour
 *	modes if necessary (!ms).  Always leave altcharset (xterm at least
 *	ignores a cursor move if we don't).
 */
void /* ARGSUSED */
__unsetattr(int checkms)
{
	int	isms;

	if (checkms) {
		if (!move_standout_mode)
			isms = 1;
		else
			isms = 0;
	} else
		isms = 1;
	__CTRACE(__CTRACE_REFRESH,
	    "__unsetattr: checkms = %d, ms = %s, wattr = %08x\n",
	    checkms, move_standout_mode ? "TRUE" : "FALSE", curscr->wattr);

	/*
	 * Don't leave the screen in standout mode (check against ms).  Check
	 * to see if we also turn off underscore, attributes and colour.
	 */
	if (curscr->wattr & __STANDOUT && isms) {
		tputs(exit_standout_mode, 0, __cputchar);
		curscr->wattr &= __mask_se;
	}
	/*
	 * Don't leave the screen in underscore mode (check against ms).
	 * Check to see if we also turn off attributes.  Assume that we
	 * also turn off colour.
	 */
	if (curscr->wattr & __UNDERSCORE && isms) {
		tputs(exit_underline_mode, 0, __cputchar);
		curscr->wattr &= __mask_ue;
	}
	/*
	 * Don't leave the screen with attributes set (check against ms).
	 * Assume that also turn off colour.
	 */
	if (curscr->wattr & __TERMATTR && isms) {
		tputs(exit_attribute_mode, 0, __cputchar);
		curscr->wattr &= __mask_me;
	}
	/* Don't leave the screen with altcharset set (don't check ms). */
	if (curscr->wattr & __ALTCHARSET) {
		tputs(exit_alt_charset_mode, 0, __cputchar);
		curscr->wattr &= ~__ALTCHARSET;
	}
	/* Don't leave the screen with colour set (check against ms). */
	if (__using_color && isms)
		__unset_color(curscr);
}

/* compare two line segments */
static int
lineeq(__LDATA *xl, __LDATA *yl, size_t len)
{
	int i = 0;
	__LDATA *xp = xl, *yp = yl;

	for (i = 0; i < len; i++, xp++, yp++) {
		if (!_cursesi_celleq(xp, yp))
			return 0;
	}
	return 1;
}

#ifdef HAVE_WCHAR
/*
 * Output the non-spacing characters associated with the given character
 * cell to the screen.
 */

void
__cursesi_putnsp(nschar_t *nsp, const int wy, const int wx)
{
	nschar_t *p;

	/* this shuts up gcc warnings about wx and wy not being used */
	if (wx > wy) {
	}

	p = nsp;
	while (p != NULL) {
		__cputwchar((int)p->ch);
		__CTRACE(__CTRACE_REFRESH,
		    "_cursesi_putnsp: (%d,%d) non-spacing putwchar(0x%x)\n",
		    wy, wx - 1, p->ch);
		p = p->next;
	}
}

#endif /* HAVE_WCHAR */
