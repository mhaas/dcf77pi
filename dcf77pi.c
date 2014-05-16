/*
Copyright (c) 2013-2014 René Ladan. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

#include "input.h"
#include "decode_time.h"
#include "decode_alarm.h"
#include "config.h"
#include "setclock.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <ncurses.h>

#define MAXBUF 255

WINDOW *input_win;
WINDOW *decode_win;
WINDOW *alarm_win;
WINDOW *main_win;
int bitpos;
int old_bitpos = -1; /* timer for statusbar inactive */
int input_mode = 0;  /* normal input (statusbar keys) or string input */
char keybuf[MAXBUF]; /* accumulator for string input */
uint8_t input_count, input_xpos;
int msglen;

int
init_curses(void)
{
	initscr();
	if (has_colors() == FALSE)
		return -1;
	start_color();
	init_pair(1, COLOR_RED, COLOR_BLACK);
	init_pair(2, COLOR_GREEN, COLOR_BLACK);
	init_pair(3, COLOR_YELLOW, COLOR_BLACK); /* turn on A_BOLD */
	init_pair(4, COLOR_BLUE, COLOR_BLACK);
	init_pair(7, COLOR_WHITE, COLOR_BLACK);
	init_pair(8, COLOR_BLACK, COLOR_BLACK); /* A_INVIS does not work? */

	noecho();
	nonl();
	cbreak();
	nodelay(stdscr, TRUE);
	keypad(stdscr, TRUE);
	curs_set(0);
	refresh(); /* prevent clearing windows upon getch() / refresh() */

	return 0;
}

void
statusbar(WINDOW *win, int bitpos, char *fmt, ...)
{
	va_list ap;

	old_bitpos = bitpos;

	wmove(win, 1, 0);
	va_start(ap, fmt);
	vw_printw(win, fmt, ap);
	va_end(ap);
	wclrtoeol(win);
	wrefresh(win);
}

void
draw_keys(WINDOW *win)
{
	mvwprintw(win, 1, 0, "[Q] -> quit [L] -> change log file"
	    " [S] -> toggle time sync");
	wclrtoeol(win);
	mvwchgat(win, 1, 1, 1, A_BOLD, 4, NULL); /* [Q] */
	mvwchgat(win, 1, 13, 1, A_BOLD, 4, NULL); /* [L] */
	mvwchgat(win, 1, 36, 1, A_BOLD, 4, NULL); /* [S] */
	wrefresh(win);
}

void
check_timer(WINDOW *win, int bitpos)
{
	if (old_bitpos != -1 && (bitpos % 60 == (old_bitpos + 2) % 60 ||
	    (old_bitpos == 57 && bitpos == 0))) {
		/*
		 * Time for status text passed, cannot use *sleep()
		 * in statusbar() because that pauses reception
		 */
		old_bitpos = -1;
		draw_keys(win);
	}
}

void
input_line(WINDOW *win, char *msg)
{
	mvwprintw(win, 1, 0, "%s", msg);
	wclrtoeol(win);
	wrefresh(win);
	input_mode = 1;
	input_count = 0;
	input_xpos = strlen(msg);
	msglen = strlen(msg);
}

void
end_input(WINDOW *win)
{
	keybuf[input_count] = '\0'; /* terminate to prevent overflow */
	draw_keys(win);
	input_mode = -1;
}

void
process_key(WINDOW *win, int inkey)
{
	char dispbuf[80];

	if (inkey == KEY_BACKSPACE || inkey == '\b' || inkey == 127) {
		if (input_count > 0) {
			input_count--;
			input_xpos--;
			if (input_xpos > 78) {
				/* Shift display line one character to right */
				(void)strncpy(dispbuf, keybuf +
				    (input_xpos - 79), 79 - msglen);
				dispbuf[80 - msglen] = '\0';
				mvwprintw(win, 1, msglen + 1, "%s", dispbuf);
			} else {
				wmove(win, 1, input_xpos + 1);
				wclrtoeol(win);
			}
			wrefresh(win);
		}
	} else if ((inkey == KEY_ENTER || inkey == '\r' || inkey == '\n') ||
	    input_count == MAXBUF-1)
		end_input(win);
	else {
		keybuf[input_count++] = inkey;
		input_xpos++;
		if (input_xpos > 79) {
			/* Shift displayed line one character to the left */
			(void)strncpy(dispbuf, keybuf +
			    (input_xpos - 79), 79 - msglen);
			dispbuf[80 - msglen] = '\0';
			mvwprintw(win, 1, msglen + 1, "%s", dispbuf);
		} else
			mvwprintw(win, 1, input_xpos, "%c", inkey);
		wrefresh(win);
	}
}

int
get_inputmode(void)
{
	return input_mode;
}

void
set_inputmode(int mode)
{
	input_mode = mode;
}

char *
get_keybuf(void)
{
	return keybuf;
}
void
curses_cleanup(char *reason)
{
	if (decode_win != NULL)
		delwin(decode_win);
	if (alarm_win != NULL)
		delwin(alarm_win);
	if (input_win != NULL)
		delwin(input_win);
	if (main_win != NULL)
		delwin(main_win);
	endwin();
	if (reason != NULL)
		printf("%s", reason);
}

void
display_bit_gui(uint16_t state)
{
	int xpos, i;

	mvwprintw(input_win, 3, 1, "%2u", bitpos);

	wattron(input_win, COLOR_PAIR(2));
	if (state & GETBIT_EOM)
		mvwprintw(input_win, 3, 59, "minute   ");
	else if (state == 0 || state == GETBIT_ONE)
		mvwprintw(input_win, 3, 59, "OK       ");
	else
		mvwprintw(input_win, 3, 59, "         ");
	wattroff(input_win, COLOR_PAIR(2));

	wattron(input_win, COLOR_PAIR(1));
	if (state & GETBIT_READ)
		mvwprintw(input_win, 3, 59, "read     ");
	if (state & GETBIT_RECV)
		mvwprintw(input_win, 3, 70, "receive ");
	else if (state & GETBIT_XMIT)
		mvwprintw(input_win, 3, 70, "transmit");
	else if (state & GETBIT_RND)
		mvwprintw(input_win, 3, 70, "random  ");
	else if (state & GETBIT_IO)
		mvwprintw(input_win, 3, 70, "IO      ");
	else {
		wattron(input_win, COLOR_PAIR(2));
		mvwprintw(input_win, 3, 70, "OK      ");
		wattroff(input_win, COLOR_PAIR(2));
	}
	wattroff(input_win, COLOR_PAIR(1));

	for (xpos = bitpos + 4, i = 0; i <= bitpos; i++)
		if (is_space_bit(i))
			xpos++;

	mvwprintw(input_win, 0, xpos, "%u", get_buffer()[bitpos]);
	if (state & GETBIT_READ)
		mvwchgat(input_win, 0, xpos, 1, A_BOLD, 3, NULL);
	wrefresh(input_win);
}

void
draw_input_window(void)
{
	mvwprintw(input_win, 0, 0, "new");
	mvwprintw(input_win, 2, 0, "bit  act total          realfreq   b0"
	    "  b20  max1 increment state      radio");
	wrefresh(input_win);
}

int
switch_logfile(WINDOW *win, char **logfilename)
{
	int res;
	char *old_logfilename;

	if (*logfilename == NULL)
		*logfilename = strdup("");
	old_logfilename = strdup(*logfilename);
	free(*logfilename);
	*logfilename = strdup(get_keybuf());
	if (!strcmp(*logfilename, ".")) {
		free(*logfilename);
		*logfilename = strdup(old_logfilename);
	}

	if (strcmp(old_logfilename, *logfilename)) {
		if (strlen(old_logfilename) > 0) {
			if (close_logfile() != 0) {
				statusbar(win, bitpos,
				    "Error closing old log file");
				return errno;
			}
		}
		if (strlen(*logfilename) > 0) {
			if ((res = write_new_logfile(*logfilename)) != 0) {
				statusbar(win, bitpos, strerror(res));
				return res;
			}
		}
	}
	free(old_logfilename);
	return 0;
}

void
display_time_gui(uint32_t dt, struct tm time, uint8_t *buffer, int minlen,
    int acc_minlen)
{
	int i, xpos;

	/* display bits of previous minute */
	for (xpos = 4, i = 0; i < minlen; i++, xpos++) {
		if (i > 59)
			break;
		if (is_space_bit(i))
			xpos++;
		mvwprintw(decode_win, 0, xpos, "%u", buffer[i]);
	}
	wclrtoeol(decode_win);
	mvwchgat(decode_win, 0, 0, 80, A_NORMAL, 7, NULL);
	/* color bits depending on the results */
	mvwchgat(decode_win, 0, 4, 1, A_NORMAL, dt & DT_B0 ? 1 : 2, NULL);
	mvwchgat(decode_win, 0, 24, 2, A_NORMAL, dt & DT_DSTERR ? 1 : 2, NULL);
	mvwchgat(decode_win, 0, 29, 1, A_NORMAL, dt & DT_B20 ? 1 : 2, NULL);
	mvwchgat(decode_win, 0, 39, 1, A_NORMAL, dt & DT_MIN ? 1 : 2, NULL);
	mvwchgat(decode_win, 0, 48, 1, A_NORMAL, dt & DT_HOUR ? 1 : 2, NULL);
	mvwchgat(decode_win, 0, 76, 1, A_NORMAL, dt & DT_DATE ? 1 : 2, NULL);
	if (dt & DT_LEAPONE)
		mvwchgat(decode_win, 0, 78, 1, A_NORMAL, 3, NULL);

	/* display date and time */
	mvwprintw(decode_win, 1, 0, "%s %04d-%02d-%02d %s %02d:%02d (%6d)",
	    time.tm_isdst ? "summer" : "winter", time.tm_year, time.tm_mon,
	    time.tm_mday, get_weekday(time.tm_wday), time.tm_hour, time.tm_min,
	    acc_minlen >= 1e6 ? 999999 : acc_minlen);
	mvwchgat(decode_win, 1, 0, 80, A_NORMAL, 7, NULL);
	/* color date/time string depending on the results */
	if (dt & DT_DSTJUMP)
		mvwchgat(decode_win, 1, 0, 6, A_BOLD, 3, NULL);
	if (dt & DT_YEARJUMP)
		mvwchgat(decode_win, 1, 7, 4, A_BOLD, 3, NULL);
	if (dt & DT_MONTHJUMP)
		mvwchgat(decode_win, 1, 12, 2, A_BOLD, 3, NULL);
	if (dt & DT_MDAYJUMP)
		mvwchgat(decode_win, 1, 15, 2, A_BOLD, 3, NULL);
	if (dt & DT_WDAYJUMP)
		mvwchgat(decode_win, 1, 18, 3, A_BOLD, 3, NULL);
	if (dt & DT_HOURJUMP)
		mvwchgat(decode_win, 1, 22, 2, A_BOLD, 3, NULL);
	if (dt & DT_MINJUMP)
		mvwchgat(decode_win, 1, 25, 2, A_BOLD, 3, NULL);

	/* flip lights depending on the results */
	if ((dt & DT_XMIT) == 0)
		mvwchgat(decode_win, 1, 39, 6, A_NORMAL, 8, NULL);
	if ((dt & ANN_CHDST) == 0)
		mvwchgat(decode_win, 1, 46, 3, A_NORMAL, 8, NULL);
	if (dt & DT_CHDST)
		mvwchgat(decode_win, 1, 46, 3, A_NORMAL, 2, NULL);
	else if (dt & DT_CHDSTERR)
		mvwchgat(decode_win, 1, 46, 3, A_BOLD, 3, NULL);
	if ((dt & ANN_LEAP) == 0)
		mvwchgat(decode_win, 1, 50, 4, A_NORMAL, 8, NULL);
	if (dt & DT_LEAP)
		mvwchgat(decode_win, 1, 50, 4, A_NORMAL, 2, NULL);
	else if (dt & DT_LEAPERR)
		mvwchgat(decode_win, 1, 50, 4, A_BOLD, 3, NULL);
	if (dt & DT_LONG) {
		mvwprintw(decode_win, 1, 56, "long ");
		mvwchgat(decode_win, 1, 56, 5, A_NORMAL, 1, NULL);
	}
	else if (dt & DT_SHORT) {
		mvwprintw(decode_win, 1, 56, "short");
		mvwchgat(decode_win, 1, 56, 5, A_NORMAL, 1, NULL);
	}
	else
		mvwchgat(decode_win, 1, 56, 5, A_NORMAL, 8, NULL);

	wrefresh(decode_win);
}

void
draw_time_window(void)
{
	mvwprintw(decode_win, 0, 0, "old");
	mvwprintw(decode_win, 1, 39, "txcall dst leap");
	mvwchgat(decode_win, 1, 39, 15, A_NORMAL, 8, NULL);
	wrefresh(decode_win);
}

void
show_civbuf_gui(uint8_t *buf)
{
	int i;

	for (i = 0; i < 40; i++)
		mvwprintw(alarm_win, 0, i + 14, "%u", buf[i]);
	wclrtoeol(alarm_win);
	wrefresh(alarm_win);
}

void
display_alarm_gui(struct alm alarm)
{
	wattron(alarm_win, COLOR_PAIR(3) | A_BOLD);
	mvwprintw(alarm_win, 1, 22,
	    "0x%1x 0x%1x 0x%1x 0x%1x 0x%03x 0x%1x 0x%03x 0x%1x",
	    alarm.ds1, alarm.ps1, alarm.ds2, alarm.ps2,
	    alarm.dl1, alarm.pl1, alarm.dl2, alarm.pl2);
	wattroff(alarm_win, COLOR_PAIR(3) | A_BOLD);
	wclrtoeol(alarm_win);
	wrefresh(alarm_win);
}

void
display_alarm_error_gui(void)
{
	wattron(alarm_win, COLOR_PAIR(1));
	mvwprintw(alarm_win, 1, 22, "error");
	wattroff(alarm_win, COLOR_PAIR(1));
	wclrtoeol(alarm_win);
	wrefresh(alarm_win);
}

void
clear_alarm_gui(void)
{
	wattron(alarm_win, COLOR_PAIR(2));
	mvwprintw(alarm_win, 1, 22, "none");
	wattroff(alarm_win, COLOR_PAIR(2));
	wclrtoeol(alarm_win);
	wrefresh(alarm_win);
}

void
draw_alarm_window(void)
{
	mvwprintw(alarm_win, 0, 0, "Civil buffer:");
	mvwprintw(alarm_win, 1, 0, "German civil warning:");
	wrefresh(alarm_win);
}

int
main(int argc, char *argv[])
{
	uint8_t indata[40], civbuf[40];
	struct bitinfo bitinf;
	uint16_t bit;
	struct tm time, oldtime;
	struct alm civwarn;
	int minlen = 0, acc_minlen = 0, old_acc_minlen;
	uint32_t dt = 0;
	uint8_t civ1 = 0, civ2 = 0;
	int init = 1, init2 = 1;
	int res, settime = 0;
	char *logfilename;
	int change_logfile = 0;
	int inkey;

	logfilename = NULL;

	res = read_config_file(ETCDIR"/config.txt");
	if (res != 0) {
		/* non-existent file? */
		cleanup();
		return res;
	}
	logfilename = get_config_value("outlogfile");
	if (logfilename != NULL && strlen(logfilename) != 0) {
		res = write_new_logfile(logfilename);
		if (res != 0) {
			perror("fopen (logfile)");
			return res;
		}
	}
	res = set_mode_live();
	if (res != 0) {
		/* something went wrong */
		cleanup();
		return res;
	}
	init_time();

	/* no weird values please */
	bzero(indata, sizeof(indata));
	bzero(&time, sizeof(time));
	bzero(&civbuf, sizeof(civbuf));
	bzero(&bitinf, sizeof(bitinf));

	decode_win = NULL;
	alarm_win = NULL;
	input_win = NULL;
	main_win = NULL;

	if (init_curses())
		curses_cleanup("No required color support.\n");

	/* allocate windows */
	decode_win = newwin(2, 80, 0, 0);
	if (decode_win == NULL) {
		curses_cleanup("Creating decode_win failed.\n");
		return 0;
	}
	alarm_win = newwin(2, 80, 3, 0);
	if (alarm_win == NULL) {
		curses_cleanup("Creating alarm_win failed.\n");
		return 0;
	}
	input_win = newwin(4, 80, 6, 0);
	if (input_win == NULL) {
		curses_cleanup("Creating input_win failed.\n");
		return 0;
	}
	main_win = newwin(2, 80, 23, 0);
	if (main_win == NULL) {
		curses_cleanup("Creating main_win failed.\n");
		return 0;
	}
	/* draw initial screen */
	draw_time_window();
	draw_alarm_window();
	draw_input_window();
	draw_keys(main_win);

	for (;;) {
		bit = get_bit_live(&bitinf);
		mvwprintw(input_win, 3, 4, "%4u  %4u (%5.1f%%) %8.3f"
		    " %4u %4u %4.1f%%  %8.6f", bitinf.tlow, bitinf.t, bitinf.frac * 100,
		    bitinf.realfreq, (int)bitinf.bit0, (int)bitinf.bit20, bitinf.maxone * 100, bitinf.a);
		if (bitinf.freq_reset)
			mvwchgat(input_win, 3, 24, 8, A_BOLD, 3, NULL);
		else
			mvwchgat(input_win, 3, 24, 8, A_NORMAL, 7, NULL);
		wrefresh(input_win);
		inkey = getch();
		if (get_inputmode() == 0 && inkey != ERR)
			switch (inkey) {
			case 'Q':
				bit |= GETBIT_EOD; /* quit main loop */
				break;
			case 'L':
				inkey = ERR; /* prevent key repeat */
				mvwprintw(main_win, 0, 0,
				    "Current log (.): %s", (logfilename
				        && strlen(logfilename) > 0) ?
					logfilename : "(none)");
				input_line(main_win,
				    "Log file (empty for none):");
				change_logfile = 1;
				break;
			case 'S':
				settime = 1 - settime;
				statusbar(main_win, bitpos,
				    "Time synchronization %s",
				    settime ? "on" : "off");
				break;
			}
		while (get_inputmode() == 1 && inkey != ERR) {
			process_key(main_win, inkey);
			inkey = getch();
		}
		if (bit & GETBIT_EOD)
			break;

		if (bit & (GETBIT_RECV | GETBIT_XMIT | GETBIT_RND))
			acc_minlen += 2500;
		else
			acc_minlen += 1000;

		bitpos = get_bitpos();
		check_timer(main_win, bitpos);
		if (get_inputmode() == -1) {
			if (change_logfile) {
				wmove(main_win, 0, 0);
				wclrtoeol(main_win);
				wrefresh(main_win);
				if (switch_logfile(main_win,
				    &logfilename))
					bit = GETBIT_EOD; /* error */
				change_logfile = 0;
			}
			set_inputmode(0);
		}

		if (bit & GETBIT_EOM) {
			/* handle the missing minute marker */
			minlen = bitpos + 1;
			acc_minlen += 1000;
		}
		display_bit_gui(bit);

		if (init == 0) {
			switch (time.tm_min % 3) {
			case 0:
				/* copy civil warning data */
				if (bitpos > 1 && bitpos < 8)
					indata[bitpos - 2] = bit & GETBIT_ONE;
					/* 2..7 -> 0..5 */
				if (bitpos > 8 && bitpos < 15)
					indata[bitpos - 3] = bit & GETBIT_ONE;
					/* 9..14 -> 6..11 */

				/* copy civil warning flags */
				if (bitpos == 1)
					civ1 = bit & GETBIT_ONE;
				if (bitpos == 8)
					civ2 = bit & GETBIT_ONE;
				break;
			case 1:
				/* copy civil warning data */
				if (bitpos > 0 && bitpos < 15)
					indata[bitpos + 11] = bit & GETBIT_ONE;
					/* 1..14 -> 12..25 */
				break;
			case 2:
				/* copy civil warning data */
				if (bitpos > 0 && bitpos < 15)
					indata[bitpos + 25] = bit & GETBIT_ONE;
					/* 1..14 -> 26..39 */
				if (bitpos == 15)
					memcpy(civbuf, indata, sizeof(civbuf));
					/* take snapshot of civil warning buffer */
				break;
			}
		}

		bit = next_bit();
		if (get_bitpos() == 0) {
			mvwdelch(input_win, 0, 4);
			wclrtoeol(input_win);
		}
		if (bit & GETBIT_TOOLONG) {
			minlen = 61;
			/*
			 * leave acc_minlen alone,
			 * any missing marker already processed
			 */
			wattron(input_win, COLOR_PAIR(1));
			mvwprintw(input_win, 3, 59, "no minute");
			wattroff(input_win, COLOR_PAIR(1));
		}
		wrefresh(input_win);

		if (bit & (GETBIT_EOM | GETBIT_TOOLONG)) {
			old_acc_minlen = acc_minlen;
			if (init == 1 || minlen >= 59)
				memcpy((void *)&oldtime, (const void *)&time,
				    sizeof(time));
			dt = decode_time(init, init2, minlen, get_buffer(),
			    &time, &acc_minlen);

			if (time.tm_min % 3 == 0 && init == 0) {
				decode_alarm(civbuf, &civwarn);
				show_civbuf_gui(civbuf);
				if (civ1 == 1 && civ2 == 1)
					display_alarm_gui(civwarn);
				if (civ1 != civ2)
					display_alarm_error_gui();
				if (civ1 == 0 && civ2 == 0)
					clear_alarm_gui();
			}

			display_time_gui(dt, time, get_buffer(),
			    minlen, old_acc_minlen);

			if (settime == 1 && init == 0 && init2 == 0 &&
			    ((dt & ~(DT_XMIT | DT_CHDST | DT_LEAP)) == 0) &&
			    ((bit & ~(GETBIT_ONE | GETBIT_EOM)) == 0))
				switch (setclock(time)) {
				case -1:
					statusbar(main_win, bitpos,
					    "mktime() failed!");
					bit |= GETBIT_EOD; /* error */
					break;
				case -2:
					statusbar(main_win, bitpos,
					    "settimeofday(): %s",
					    strerror(errno));
					bit |= GETBIT_EOD; /* error */
					break;
				default:
					statusbar(main_win, bitpos, "Time set");
					break;
				}
			if (init == 1 || !((dt & DT_LONG) || (dt & DT_SHORT)))
				acc_minlen = 0; /* really a new minute */
			if (init == 0 && init2 == 1)
				init2 = 0;
			if (init == 1)
				init = 0;
		}
	}

	cleanup();
	curses_cleanup(NULL);
	if (logfilename != NULL)
		free(logfilename);
	return 0;
}
