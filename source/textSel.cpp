/*******************************************************************************
*                                                                              *
* textSel.c - Selection and clipboard routines for NEdit text widget           *
*                                                                              *
* Copyright (C) 1999 Mark Edel                                                 *
*                                                                              *
* This is free software; you can redistribute it and/or modify it under the    *
* terms of the GNU General Public License as published by the Free Software    *
* Foundation; either version 2 of the License, or (at your option) any later   *
* version. In addition, you may distribute version of this program linked to   *
* Motif or Open Motif. See README for details.                                 *
*                                                                              *
* This software is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        *
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License        *
* for more details.                                                            *
*                                                                              *
* You should have received a copy of the GNU General Public License along with *
* software; if not, write to the Free Software Foundation, Inc., 59 Temple     *
* Place, Suite 330, Boston, MA  02111-1307 USA                                 *
*                                                                              *
* Nirvana Text Editor                                                          *
* Dec. 15, 1995                                                                *
*                                                                              *
* Written by Mark Edel                                                         *
*                                                                              *
*******************************************************************************/

#include "textSel.h"
#include "textP.h"
#include "text.h"
#include "textDisp.h"
#include "TextBuffer.h"
#include "MotifHelper.h"
#include "../util/misc.h"

#include <cstdio>
#include <cstring>
#include <climits>

#include <Xm/Xm.h>
#include <Xm/CutPaste.h>
#include <Xm/Text.h>
#include <X11/Xatom.h>
#if XmVersion >= 1002
#include <Xm/PrimitiveP.h>
#endif

#define N_SELECT_TARGETS 7
#define N_ATOMS 11
enum atomIndex { A_TEXT, A_TARGETS, A_MULTIPLE, A_TIMESTAMP, A_INSERT_SELECTION, A_DELETE, A_CLIPBOARD, A_INSERT_INFO, A_ATOM_PAIR, A_MOTIF_DESTINATION, A_COMPOUND_TEXT };

/* Results passed back to the convert proc processing an INSERT_SELECTION
   request, by getInsertSelection when the selection to insert has been
   received and processed */
enum insertResultFlags { INSERT_WAITING, UNSUCCESSFUL_INSERT, SUCCESSFUL_INSERT };

/* Actions for selection notify event handler upon receiving confermation
   of a successful convert selection request */
enum selectNotifyActions { UNSELECT_SECONDARY, REMOVE_SECONDARY, EXCHANGE_SECONDARY };

/* temporary structure for passing data to the event handler for completing
   selection requests (the hard way, via xlib calls) */
struct selectNotifyInfo {
	int action;
	XtIntervalId timeoutProcID;
	Time timeStamp;
	Widget widget;
	char *actionText;
	int length;
};

static void modifiedCB(int pos, int nInserted, int nDeleted, int nRestyled, view::string_view deletedText, void *cbArg);
static void sendSecondary(Widget w, Time time, Atom sel, int action, char *actionText, int actionTextLen);
static void getSelectionCB(Widget w, XtPointer clientData, Atom *selType, Atom *type, XtPointer value, unsigned long *length, int *format);
static void getInsertSelectionCB(Widget w, XtPointer clientData, Atom *selType, Atom *type, XtPointer value, unsigned long *length, int *format);
static void getExchSelCB(Widget w, XtPointer clientData, Atom *selType, Atom *type, XtPointer value, unsigned long *length, int *format);
static Boolean convertSelectionCB(Widget w, Atom *selType, Atom *target, Atom *type, XtPointer *value, unsigned long *length, int *format);
static void loseSelectionCB(Widget w, Atom *selType);
static Boolean convertSecondaryCB(Widget w, Atom *selType, Atom *target, Atom *type, XtPointer *value, unsigned long *length, int *format);
static void loseSecondaryCB(Widget w, Atom *selType);
static Boolean convertMotifDestCB(Widget w, Atom *selType, Atom *target, Atom *type, XtPointer *value, unsigned long *length, int *format);
static void loseMotifDestCB(Widget w, Atom *selType);
static void selectNotifyEH(Widget w, XtPointer data, XEvent *event, Boolean *continueDispatch);
static void selectNotifyTimerProc(XtPointer clientData, XtIntervalId *id);
static Atom getAtom(Display *display, int atomNum);

/*
** Designate text widget "w" to be the selection owner for primary selections
** in its attached buffer (a buffer can be attached to multiple text widgets).
*/
void HandleXSelections(Widget w) {
	auto buf = reinterpret_cast<TextWidget>(w)->text.textD->buffer;

	/* Remove any existing selection handlers for other widgets */
	for (const auto &pair : buf->modifyProcs_) {
		if (pair.first == modifiedCB) {
			buf->BufRemoveModifyCB(pair.first, pair.second);
			break;
		}
	}

	/* Add a handler with this widget as the CB arg (and thus the sel. owner) */
	reinterpret_cast<TextWidget>(w)->text.textD->buffer->BufAddModifyCB(modifiedCB, w);
}

/*
** Discontinue ownership of selections for widget "w"'s attached buffer
** (if "w" was the designated selection owner)
*/
void StopHandlingXSelections(Widget w) {
	auto buf = reinterpret_cast<TextWidget>(w)->text.textD->buffer;


	for (auto it = buf->modifyProcs_.begin(); it != buf->modifyProcs_.end(); ++it) {
		auto &pair = *it;
		if (pair.first == modifiedCB && pair.second == w) {
			buf->modifyProcs_.erase(it);
			return;
		}
	}
}

/*
** Copy the primary selection to the clipboard
*/
void CopyToClipboard(Widget w, Time time) {
	long itemID = 0;
	XmString s;
	int stat, length;

	/* Get the selected text, if there's no selection, do nothing */
	std::string text = reinterpret_cast<TextWidget>(w)->text.textD->buffer->BufGetSelectionTextEx();
	if (text.empty()) {
		return;
	}

	/* If the string contained ascii-nul characters, something else was
	   substituted in the buffer.  Put the nulls back */
	length = text.size();
	reinterpret_cast<TextWidget>(w)->text.textD->buffer->BufUnsubstituteNullCharsEx(text);

	/* Shut up LessTif */
	if (SpinClipboardLock(XtDisplay(w), XtWindow(w)) != ClipboardSuccess) {
		return;
	}

	/* Use the XmClipboard routines to copy the text to the clipboard.
	   If errors occur, just give up.  */
	s = XmStringCreateSimpleEx("NEdit");
	stat = SpinClipboardStartCopy(XtDisplay(w), XtWindow(w), s, time, w, nullptr, &itemID);
	XmStringFree(s);
	if (stat != ClipboardSuccess) {
		SpinClipboardUnlock(XtDisplay(w), XtWindow(w));
		return;
	}

	/* Note that we were previously passing length + 1 here, but I suspect
	   that this was inconsistent with the somewhat ambiguous policy of
	   including a terminating null but not mentioning it in the length */

	if (SpinClipboardCopy(XtDisplay(w), XtWindow(w), itemID, (String) "STRING", (char *)text.c_str(), length, 0, nullptr) != ClipboardSuccess) {
		SpinClipboardEndCopy(XtDisplay(w), XtWindow(w), itemID);
		SpinClipboardUnlock(XtDisplay(w), XtWindow(w));
		return;
	}

	SpinClipboardEndCopy(XtDisplay(w), XtWindow(w), itemID);
	SpinClipboardUnlock(XtDisplay(w), XtWindow(w));
}

/*
** Insert the X PRIMARY selection (from whatever window currently owns it)
** at the cursor position.
*/
void InsertPrimarySelection(Widget w, Time time, int isColumnar) {
	static int isColFlag;

	/* Theoretically, strange things could happen if the user managed to get
	   in any events between requesting receiving the selection data, however,
	   getSelectionCB simply inserts the selection at the cursor.  Don't
	   bother with further measures until real problems are observed. */
	isColFlag = isColumnar;
	XtGetSelectionValue(w, XA_PRIMARY, XA_STRING, getSelectionCB, &isColFlag, time);
}

/*
** Insert the secondary selection at the motif destination by initiating
** an INSERT_SELECTION request to the current owner of the MOTIF_DESTINATION
** selection.  Upon completion, unselect the secondary selection.  If
** "removeAfter" is true, also delete the secondary selection from the
** widget's buffer upon completion.
*/
void SendSecondarySelection(Widget w, Time time, int removeAfter) {
	sendSecondary(w, time, getAtom(XtDisplay(w), A_MOTIF_DESTINATION), removeAfter ? REMOVE_SECONDARY : UNSELECT_SECONDARY, nullptr, 0);
}

/*
** Exchange Primary and secondary selections (to be called by the widget
** with the secondary selection)
*/
void ExchangeSelections(Widget w, Time time) {
	if (!reinterpret_cast<TextWidget>(w)->text.textD->buffer->secondary_.selected)
		return;

	/* Initiate an long series of events: 1) get the primary selection,
	   2) replace the primary selection with this widget's secondary, 3) replace
	   this widget's secondary with the text returned from getting the primary
	   selection.  This could be done with a much more efficient MULTIPLE
	   request following ICCCM conventions, but the X toolkit MULTIPLE handling
	   routines can't handle INSERT_SELECTION requests inside of MULTIPLE
	   requests, because they don't allow access to the requested property atom
	   in  inside of an XtConvertSelectionProc.  It's simply not worth
	   duplicating all of Xt's selection handling routines for a little
	   performance, and this would make the code incompatible with Motif text
	   widgets */
	XtGetSelectionValue(w, XA_PRIMARY, XA_STRING, getExchSelCB, nullptr, time);
}

/*
** Insert the contents of the PRIMARY selection at the cursor position in
** widget "w" and delete the contents of the selection in its current owner
** (if the selection owner supports DELETE targets).
*/
void MovePrimarySelection(Widget w, Time time, int isColumnar) {
	static Atom targets[2] = {XA_STRING};
	static int isColFlag;
	static XtPointer clientData[2] = {&isColFlag, &isColFlag};

	targets[1] = getAtom(XtDisplay(w), A_DELETE);
	isColFlag = isColumnar;
	/* some strangeness here: the selection callback appears to be getting
	   clientData[1] for targets[0] */
	XtGetSelectionValues(w, XA_PRIMARY, targets, 2, getSelectionCB, clientData, time);
}

/*
** Insert the X CLIPBOARD selection at the cursor position.  If isColumnar,
** do an BufInsertColEx for a columnar paste instead of BufInsertEx.
*/
void InsertClipboard(Widget w, int isColumnar) {
	unsigned long length, retLength;
	auto textD = reinterpret_cast<TextWidget>(w)->text.textD;
	auto buf   = reinterpret_cast<TextWidget>(w)->text.textD->buffer;
	int cursorLineStart, column, cursorPos;
	long id = 0;

	/* Get the clipboard contents.  Note: this code originally used the
	   CLIPBOARD selection, rather than the Motif clipboard interface.  It
	   was changed because Motif widgets in the same application would hang
	   when users pasted data from nedit text widgets.  This happened because
	   the XmClipboard routines used by the widgets do blocking event reads,
	   preventing a response by a selection owner in the same application.
	   While the Motif clipboard routines as they are used below, limit the
	   size of the data that be transferred via the clipboard, and are
	   generally slower and buggier, they do preserve the clipboard across
	   widget destruction and even program termination. */
	if (SpinClipboardInquireLength(XtDisplay(w), XtWindow(w), (String) "STRING", &length) != ClipboardSuccess || length == 0) {
		/*
		 * Possibly, the clipboard can remain in a locked state after
		 * a failure, so we try to remove the lock, just to be sure.
		 */
		SpinClipboardUnlock(XtDisplay(w), XtWindow(w));
		return;
	}
	char *string = XtMalloc(length + 1);
	if (SpinClipboardRetrieve(XtDisplay(w), XtWindow(w), (String) "STRING", string, length, &retLength, &id) != ClipboardSuccess || retLength == 0) {
		XtFree(string);
		/*
		 * Possibly, the clipboard can remain in a locked state after
		 * a failure, so we try to remove the lock, just to be sure.
		 */
		SpinClipboardUnlock(XtDisplay(w), XtWindow(w));
		return;
	}
	string[retLength] = '\0';
	
	std::string contents(string, retLength);
	XtFree(string);

	/* If the string contains ascii-nul characters, substitute something
	   else, or give up, warn, and refuse */
	if (!buf->BufSubstituteNullCharsEx(contents)) {
		fprintf(stderr, "Too much binary data, text not pasted\n");
		return;
	}

	/* Insert it in the text widget */
	if (isColumnar && !buf->primary_.selected) {
		cursorPos = TextDGetInsertPosition(textD);
		cursorLineStart = buf->BufStartOfLine(cursorPos);
		column = buf->BufCountDispChars(cursorLineStart, cursorPos);
		if (reinterpret_cast<TextWidget>(w)->text.overstrike) {
			buf->BufOverlayRectEx(cursorLineStart, column, -1, contents, nullptr, nullptr);
		} else {
			buf->BufInsertColEx(column, cursorLineStart, contents, nullptr, nullptr);
		}
		textD->TextDSetInsertPosition(buf->BufCountForwardDispChars(cursorLineStart, column));
		if (reinterpret_cast<TextWidget>(w)->text.autoShowInsertPos)
			textD->TextDMakeInsertPosVisible();
	} else
		TextInsertAtCursorEx(w, contents, nullptr, True, reinterpret_cast<TextWidget>(w)->text.autoWrapPastedText);
}

/*
** Take ownership of the MOTIF_DESTINATION selection.  This is Motif's private
** selection type for designating a widget to receive the result of
** secondary quick action requests.  The NEdit text widget uses this also
** for compatibility with Motif text widgets.
*/
void TakeMotifDestination(Widget w, Time time) {
	if (reinterpret_cast<TextWidget>(w)->text.motifDestOwner || reinterpret_cast<TextWidget>(w)->text.readOnly)
		return;

	/* Take ownership of the MOTIF_DESTINATION selection */
	if (!XtOwnSelection(w, getAtom(XtDisplay(w), A_MOTIF_DESTINATION), time, convertMotifDestCB, loseMotifDestCB, nullptr)) {
		return;
	}
	reinterpret_cast<TextWidget>(w)->text.motifDestOwner = True;
}

/*
** This routine is called every time there is a modification made to the
** buffer to which this callback is attached, with an argument of the text
** widget that has been designated (by HandleXSelections) to handle its
** selections.  It checks if the status of the selection in the buffer
** has changed since last time, and owns or disowns the X selection depending
** on the status of the primary selection in the buffer.  If it is not allowed
** to take ownership of the selection, it unhighlights the text in the buffer
** (Being in the middle of a modify callback, this has a somewhat complicated
** result, since later callbacks will see the second modifications first).
*/
static void modifiedCB(int pos, int nInserted, int nDeleted, int nRestyled, view::string_view deletedText, void *cbArg) {

	(void)pos;
	(void)nInserted;
	(void)nRestyled;
	(void)nDeleted;
	(void)deletedText;

	TextWidget w = (TextWidget)cbArg;
	Time time = XtLastTimestampProcessed(XtDisplay((Widget)w));
	int selected = w->text.textD->buffer->primary_.selected;
	int isOwner = w->text.selectionOwner;

	/* If the widget owns the selection and the buffer text is still selected,
	   or if the widget doesn't own it and there's no selection, do nothing */
	if ((isOwner && selected) || (!isOwner && !selected))
		return;

	/* Don't disown the selection here.  Another application (namely: klipper)
	   may try to take it when it thinks nobody has the selection.  We then
	   lose it, making selection-based macro operations fail.  Disowning
	   is really only for when the widget is destroyed to avoid a convert
	   callback from firing at a bad time. */

	/* Take ownership of the selection */
	if (!XtOwnSelection((Widget)w, XA_PRIMARY, time, convertSelectionCB, loseSelectionCB, nullptr))
		w->text.textD->buffer->BufUnselect();
	else
		w->text.selectionOwner = True;
}

/*
** Send an INSERT_SELECTION request to "sel".
** Upon completion, do the action specified by "action" (one of enum
** selectNotifyActions) using "actionText" and freeing actionText (if
** not nullptr) when done.
*/
static void sendSecondary(Widget w, Time time, Atom sel, int action, char *actionText, int actionTextLen) {
	static Atom selInfoProp[2] = {XA_SECONDARY, XA_STRING};
	Display *disp = XtDisplay(w);
	XtAppContext context = XtWidgetToApplicationContext((Widget)w);

	/* Take ownership of the secondary selection, give up if we can't */
	if (!XtOwnSelection(w, XA_SECONDARY, time, convertSecondaryCB, loseSecondaryCB, nullptr)) {
		reinterpret_cast<TextWidget>(w)->text.textD->buffer->BufSecondaryUnselect();
		return;
	}

	/* Set up a property on this window to pass along with the
	   INSERT_SELECTION request to tell the MOTIF_DESTINATION owner what
	   selection and what target from that selection to insert */
	XChangeProperty(disp, XtWindow(w), getAtom(disp, A_INSERT_INFO), getAtom(disp, A_ATOM_PAIR), 32, PropModeReplace, (unsigned char *)selInfoProp, 2 /* 1? */);

	/* Make INSERT_SELECTION request to the owner of selection "sel"
	   to do the insert.  This must be done using XLib calls to specify
	   the property with the information about what to insert.  This
	   means it also requires an event handler to see if the request
	   succeeded or not, and a backup timer to clean up if the select
	   notify event is never returned */
	XConvertSelection(XtDisplay(w), sel, getAtom(disp, A_INSERT_SELECTION), getAtom(disp, A_INSERT_INFO), XtWindow(w), time);
	auto cbInfo = new selectNotifyInfo;
	cbInfo->action       = action;
	cbInfo->timeStamp    = time;
	cbInfo->widget       = (Widget)w;
	cbInfo->actionText   = actionText;
	cbInfo->length       = actionTextLen;
	XtAddEventHandler(w, 0, True, selectNotifyEH, cbInfo);
	cbInfo->timeoutProcID = XtAppAddTimeOut(context, XtAppGetSelectionTimeout(context), selectNotifyTimerProc, cbInfo);
}

/*
** Called when data arrives from a request for the PRIMARY selection.  If
** everything is in order, it inserts it at the cursor in the requesting
** widget.
*/
static void getSelectionCB(Widget w, XtPointer clientData, Atom *selType, Atom *type, XtPointer value, unsigned long *length, int *format) {

	(void)selType;

	auto textD = reinterpret_cast<TextWidget>(w)->text.textD;
	int isColumnar = *(int *)clientData;
	int cursorLineStart, cursorPos, column, row;

	/* Confirm that the returned value is of the correct type */
	if (*type != XA_STRING || *format != 8) {
		XtFree((char *)value);
		return;
	}

	/* Copy the string just to make space for the null character (this may
	   not be necessary, XLib documentation claims a nullptr is already added,
	   but the Xt documentation for this routine makes no such claim) */
	std::string string(static_cast<char *>(value), *length);

	/* If the string contains ascii-nul characters, substitute something
	   else, or give up, warn, and refuse */
	if (!textD->buffer->BufSubstituteNullCharsEx(string)) {
		fprintf(stderr, "Too much binary data, giving up\n");
		XtFree((char *)value);
		return;
	}

	/* Insert it in the text widget */
	if (isColumnar) {
		cursorPos = TextDGetInsertPosition(textD);
		cursorLineStart = textD->buffer->BufStartOfLine(cursorPos);
		textD->TextDXYToUnconstrainedPosition(reinterpret_cast<TextWidget>(w)->text.btnDownX, reinterpret_cast<TextWidget>(w)->text.btnDownY, &row, &column);
		textD->buffer->BufInsertColEx(column, cursorLineStart, string, nullptr, nullptr);
		textD->TextDSetInsertPosition(textD->buffer->cursorPosHint_);
	} else
		TextInsertAtCursorEx(w, string, nullptr, False, reinterpret_cast<TextWidget>(w)->text.autoWrapPastedText);

	/* The selection requstor is required to free the memory passed
	   to it via value */
	XtFree((char *)value);
}

/*
** Called when data arrives from request resulting from processing an
** INSERT_SELECTION request.  If everything is in order, inserts it at
** the cursor or replaces pending delete selection in widget "w", and sets
** the flag passed in clientData to SUCCESSFUL_INSERT or UNSUCCESSFUL_INSERT
** depending on the success of the operation.
*/
static void getInsertSelectionCB(Widget w, XtPointer clientData, Atom *selType, Atom *type, XtPointer value, unsigned long *length, int *format) {

	(void)selType;

	auto buf = reinterpret_cast<TextWidget>(w)->text.textD->buffer;
	int *resultFlag = (int *)clientData;

	/* Confirm that the returned value is of the correct type */
	if (*type != XA_STRING || *format != 8 || value == nullptr) {
		XtFree((char *)value);
		*resultFlag = UNSUCCESSFUL_INSERT;
		return;
	}

	/* Copy the string just to make space for the null character */
	std::string string(static_cast<char *>(value), *length);

	/* If the string contains ascii-nul characters, substitute something
	   else, or give up, warn, and refuse */
	if (!buf->BufSubstituteNullCharsEx(string)) {
		fprintf(stderr, "Too much binary data, giving up\n");
		XtFree((char *)value);
		return;
	}

	/* Insert it in the text widget */
	TextInsertAtCursorEx(w, string, nullptr, True, reinterpret_cast<TextWidget>(w)->text.autoWrapPastedText);
	*resultFlag = SUCCESSFUL_INSERT;

	/* This callback is required to free the memory passed to it thru value */
	XtFree((char *)value);
}

/*
** Called when data arrives from an X primary selection request for the
** purpose of exchanging the primary and secondary selections.
** If everything is in order, stores the retrieved text temporarily and
** initiates a request to replace the primary selection with this widget's
** secondary selection.
*/
static void getExchSelCB(Widget w, XtPointer clientData, Atom *selType, Atom *type, XtPointer value, unsigned long *length, int *format) {

	(void)selType;
	(void)clientData;

	/* Confirm that there is a value and it is of the correct type */
	if (*length == 0 || value == nullptr || *type != XA_STRING || *format != 8) {
		XtFree((char *)value);
		XBell(XtDisplay(w), 0);
		reinterpret_cast<TextWidget>(w)->text.textD->buffer->BufSecondaryUnselect();
		return;
	}

	/* Request the selection owner to replace the primary selection with
	   this widget's secondary selection.  When complete, replace this
	   widget's secondary selection with text "value" and free it. */
	sendSecondary(w, XtLastTimestampProcessed(XtDisplay(w)), XA_PRIMARY, EXCHANGE_SECONDARY, (char *)value, *length);
}

/*
** Selection converter procedure used by the widget when it is the selection
** owner to provide data in the format requested by the selection requestor.
**
** Note: Memory left in the *value field is freed by Xt as long as there is no
** done_proc procedure registered in the XtOwnSelection call where this
** procdeure is registered
*/
static Boolean convertSelectionCB(Widget w, Atom *selType, Atom *target, Atom *type, XtPointer *value, unsigned long *length, int *format) {
	XSelectionRequestEvent *event = XtGetSelectionRequest(w, *selType, nullptr);
	auto buf = reinterpret_cast<TextWidget>(w)->text.textD->buffer;
	Display *display = XtDisplay(w);
	Atom *targets, dummyAtom;
	unsigned long nItems, dummyULong;
	Atom *reqAtoms;
	int getFmt, result = INSERT_WAITING;
	XEvent nextEvent;

	/* target is text, string, or compound text */
	if (*target == XA_STRING || *target == getAtom(display, A_TEXT) || *target == getAtom(display, A_COMPOUND_TEXT)) {
		/* We really don't directly support COMPOUND_TEXT, but recent
		   versions gnome-terminal incorrectly ask for it, even though
		   don't declare that we do.  Just reply in string format. */
		   
		// TODO(eteran): inefficient copy here, but we plan to deprecate X Toolkit direct usage anyway
		std::string s = buf->BufGetSelectionTextEx();
		buf->BufUnsubstituteNullCharsEx(s);
		
		char *str = XtStringDup(s);
		   
		*type   = XA_STRING;
		*value  = str;
		*length = s.size();
		*format = 8;

		return True;
	}

	/* target is "TARGETS", return a list of targets we can handle */
	if (*target == getAtom(display, A_TARGETS)) {
		targets = (Atom *)XtMalloc(sizeof(Atom) * N_SELECT_TARGETS);
		targets[0] = XA_STRING;
		targets[1] = getAtom(display, A_TEXT);
		targets[2] = getAtom(display, A_TARGETS);
		targets[3] = getAtom(display, A_MULTIPLE);
		targets[4] = getAtom(display, A_TIMESTAMP);
		targets[5] = getAtom(display, A_INSERT_SELECTION);
		targets[6] = getAtom(display, A_DELETE);
		*type = XA_ATOM;
		*value = targets;
		*length = N_SELECT_TARGETS;
		*format = 32;
		return True;
	}

	/* target is "INSERT_SELECTION":  1) get the information about what
	   selection and target to use to get the text to insert, from the
	   property named in the property field of the selection request event.
	   2) initiate a get value request for the selection and target named
	   in the property, and WAIT until it completes */
	if (*target == getAtom(display, A_INSERT_SELECTION)) {
		if (reinterpret_cast<TextWidget>(w)->text.readOnly)
			return False;
		if (XGetWindowProperty(event->display, event->requestor, event->property, 0, 2, False, AnyPropertyType, &dummyAtom, &getFmt, &nItems, &dummyULong, (unsigned char **)&reqAtoms) != Success || getFmt != 32 || nItems != 2)
			return False;
		if (reqAtoms[1] != XA_STRING)
			return False;
		XtGetSelectionValue(w, reqAtoms[0], reqAtoms[1], getInsertSelectionCB, &result, event->time);
		XFree((char *)reqAtoms);
		while (result == INSERT_WAITING) {
			XtAppNextEvent(XtWidgetToApplicationContext(w), &nextEvent);
			XtDispatchEvent(&nextEvent);
		}
		*type = getAtom(display, A_INSERT_SELECTION);
		*format = 8;
		*value = nullptr;
		*length = 0;
		return result == SUCCESSFUL_INSERT;
	}

	/* target is "DELETE": delete primary selection */
	if (*target == getAtom(display, A_DELETE)) {
		buf->BufRemoveSelected();
		*length = 0;
		*format = 8;
		*type = getAtom(display, A_DELETE);
		*value = nullptr;
		return True;
	}

	/* targets TIMESTAMP and MULTIPLE are handled by the toolkit, any
	   others are unrecognized, return False */
	return False;
}

static void loseSelectionCB(Widget w, Atom *selType) {

	(void)selType;

	TextWidget tw = (TextWidget)w;
	TextSelection *sel = &tw->text.textD->buffer->primary_;
	char zeroWidth = sel->rectangular ? sel->zeroWidth : 0;

	/* For zero width rect. sel. we give up the selection but keep the
	    zero width tag. */
	tw->text.selectionOwner = False;
	tw->text.textD->buffer->BufUnselect();
	sel->zeroWidth = zeroWidth;
}

/*
** Selection converter procedure used by the widget to (temporarily) provide
** the secondary selection data to a single requestor who has been asked
** to insert it.
*/
static Boolean convertSecondaryCB(Widget w, Atom *selType, Atom *target, Atom *type, XtPointer *value, unsigned long *length, int *format) {

	(void)selType;

	auto buf = reinterpret_cast<TextWidget>(w)->text.textD->buffer;

	/* target must be string */
	if (*target != XA_STRING && *target != getAtom(XtDisplay(w), A_TEXT))
		return False;

	/* Return the contents of the secondary selection.  The memory allocated
	   here is freed by the X toolkit */
	   
	// TODO(eteran): inefficient copy here, but we plan to deprecate X Toolkit direct usage anyway
	std::string s = buf->BufGetSecSelectTextEx();
	buf->BufUnsubstituteNullCharsEx(s);
	
	char *str = XtStringDup(s);
	
	*type   = XA_STRING;
	*value  = str;
	*length = s.size();
	*format = 8;

	return True;
}

static void loseSecondaryCB(Widget w, Atom *selType) {

	(void)selType;
	(void)w;

	/* do nothing, secondary selections are transient anyhow, and it
	   will go away on its own */
}

/*
** Selection converter procedure used by the widget when it owns the Motif
** destination, to handle INSERT_SELECTION requests.
*/
static Boolean convertMotifDestCB(Widget w, Atom *selType, Atom *target, Atom *type, XtPointer *value, unsigned long *length, int *format) {
	XSelectionRequestEvent *event = XtGetSelectionRequest(w, *selType, nullptr);
	Display *display = XtDisplay(w);
	Atom *targets, dummyAtom;
	unsigned long nItems, dummyULong;
	Atom *reqAtoms;
	int getFmt, result = INSERT_WAITING;
	XEvent nextEvent;

	/* target is "TARGETS", return a list of targets it can handle */
	if (*target == getAtom(display, A_TARGETS)) {
		targets = (Atom *)XtMalloc(sizeof(Atom) * 3);
		targets[0] = getAtom(display, A_TARGETS);
		targets[1] = getAtom(display, A_TIMESTAMP);
		targets[2] = getAtom(display, A_INSERT_SELECTION);
		*type = XA_ATOM;
		*value = targets;
		*length = 3;
		*format = 32;
		return True;
	}

	/* target is "INSERT_SELECTION":  1) get the information about what
	   selection and target to use to get the text to insert, from the
	   property named in the property field of the selection request event.
	   2) initiate a get value request for the selection and target named
	   in the property, and WAIT until it completes */
	if (*target == getAtom(display, A_INSERT_SELECTION)) {
		if (reinterpret_cast<TextWidget>(w)->text.readOnly)
			return False;
		if (XGetWindowProperty(event->display, event->requestor, event->property, 0, 2, False, AnyPropertyType, &dummyAtom, &getFmt, &nItems, &dummyULong, (unsigned char **)&reqAtoms) != Success || getFmt != 32 || nItems != 2)
			return False;
		if (reqAtoms[1] != XA_STRING)
			return False;
		XtGetSelectionValue(w, reqAtoms[0], reqAtoms[1], getInsertSelectionCB, &result, event->time);
		XFree((char *)reqAtoms);
		while (result == INSERT_WAITING) {
			XtAppNextEvent(XtWidgetToApplicationContext(w), &nextEvent);
			XtDispatchEvent(&nextEvent);
		}
		*type = getAtom(display, A_INSERT_SELECTION);
		*format = 8;
		*value = nullptr;
		*length = 0;
		return result == SUCCESSFUL_INSERT;
	}

	/* target TIMESTAMP is handled by the toolkit and not passed here, any
	   others are unrecognized */
	return False;
}

static void loseMotifDestCB(Widget w, Atom *selType) {

	(void)selType;

	reinterpret_cast<TextWidget>(w)->text.motifDestOwner = False;
	if (reinterpret_cast<TextWidget>(w)->text.textD->cursorStyle == CARET_CURSOR)
		reinterpret_cast<TextWidget>(w)->text.textD->TextDSetCursorStyle(DIM_CURSOR);
}

/*
** Event handler for SelectionNotify events, to finish off INSERT_SELECTION
** requests which must be done through the lower
** level (and more complicated) XLib selection mechanism.  Matches the
** time stamp in the request against the time stamp stored when the selection
** request was made to find the selectionNotify event that it was installed
** to catch.  When it finds the correct event, it does the action it was
** installed to do, and removes itself and its backup timer (which would do
** the clean up if the selectionNotify event never arrived.)
*/
static void selectNotifyEH(Widget w, XtPointer data, XEvent *event, Boolean *continueDispatch) {

	(void)continueDispatch;

	auto buf    = reinterpret_cast<TextWidget>(w)->text.textD->buffer;
	auto e      = reinterpret_cast<XSelectionEvent *>(event);
	auto cbInfo = static_cast<selectNotifyInfo *>(data);
	int selStart, selEnd;

	/* Check if this was the selection request for which this handler was
	   set up, if not, do nothing */
	if (event->type != SelectionNotify || e->time != cbInfo->timeStamp)
		return;

	/* The time stamp matched, remove this event handler and its
	   backup timer procedure */
	XtRemoveEventHandler(w, 0, True, selectNotifyEH, data);
	XtRemoveTimeOut(cbInfo->timeoutProcID);

	/* Check if the request succeeded, if not, beep, remove any existing
	   secondary selection, and return */
	if (e->property == None) {
		XBell(XtDisplay(w), 0);
		buf->BufSecondaryUnselect();
		XtDisownSelection(w, XA_SECONDARY, e->time);
		XtFree(cbInfo->actionText);
		delete cbInfo;
		return;
	}

	/* Do the requested action, if the action is exchange, also clean up
	   the properties created for returning the primary selection and making
	   the MULTIPLE target request */
	if (cbInfo->action == REMOVE_SECONDARY) {
		buf->BufRemoveSecSelect();
	} else if (cbInfo->action == EXCHANGE_SECONDARY) {
		std::string string(cbInfo->actionText, cbInfo->length);

		selStart = buf->secondary_.start;
		if (buf->BufSubstituteNullCharsEx(string)) {
			buf->BufReplaceSecSelectEx(string);
			if (buf->secondary_.rectangular) {
				/*... it would be nice to re-select, but probably impossible */
				reinterpret_cast<TextWidget>(w)->text.textD->TextDSetInsertPosition(buf->cursorPosHint_);
			} else {
				selEnd = selStart + cbInfo->length;
				buf->BufSelect(selStart, selEnd);
				reinterpret_cast<TextWidget>(w)->text.textD->TextDSetInsertPosition(selEnd);
			}
		} else {
			fprintf(stderr, "Too much binary data\n");
		}
	}
	buf->BufSecondaryUnselect();
	XtDisownSelection(w, XA_SECONDARY, e->time);
	XtFree(cbInfo->actionText);
	delete cbInfo;
}

/*
** Xt timer procedure for timeouts on XConvertSelection requests, cleans up
** after a complete failure of the selection mechanism to return a selection
** notify event for a convert selection request
*/
static void selectNotifyTimerProc(XtPointer clientData, XtIntervalId *id) {

	(void)id;

	auto cbInfo = static_cast<selectNotifyInfo *>(clientData);
	TextBuffer *buf = ((TextWidget)cbInfo->widget)->text.textD->buffer;

	fprintf(stderr, "NEdit: timeout on selection request\n");
	XtRemoveEventHandler(cbInfo->widget, 0, True, selectNotifyEH, cbInfo);
	buf->BufSecondaryUnselect();
	XtDisownSelection(cbInfo->widget, XA_SECONDARY, cbInfo->timeStamp);
	XtFree(cbInfo->actionText);
	delete cbInfo;
}

/*
** Maintain a cache of interned atoms.  To reference one, use the constant
** from the enum, atomIndex, above.
*/
static Atom getAtom(Display *display, int atomNum) {
	static Atom atomList[N_ATOMS] = {0};
	static const char *atomNames[N_ATOMS] = {"TEXT", "TARGETS", "MULTIPLE", "TIMESTAMP", "INSERT_SELECTION", "DELETE", "CLIPBOARD", "INSERT_INFO", "ATOM_PAIR", "MOTIF_DESTINATION", "COMPOUND_TEXT"};

	if (atomList[atomNum] == 0)
		atomList[atomNum] = XInternAtom(display, atomNames[atomNum], False);
	return atomList[atomNum];
}
