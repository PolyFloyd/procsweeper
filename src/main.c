/*
 * Copyright (c) 2014 PolyFloyd
 */

#include <getopt.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "board.h"
#include "proc.h"
#include "util.h"

#define BOARD_WIDTH  80
#define BOARD_HEIGHT 40

#define SYM_EMPTY    '.'
#define SYM_MINE     'X'
#define SYM_UNKNOWN  '?'
#define SYM_UNTURNED '#'

char mode_initonly = 0;
char mode_nokill   = 0;
char mode_system   = 0;
int  kill_signal   = SIGINT;

void parse_cli(int argc, char **argv);
void init(void);
void cleanup(void);
char procfilter(proc_t *proc);

int main(int argc, char **argv) {
	parse_cli(argc, argv);
	init();

	int cur_x = 0;
	int cur_y = 0;

	board_t board;
	board_init(&board, BOARD_WIDTH, BOARD_HEIGHT, BOARD_WIDTH * BOARD_HEIGHT / 8);

	bool run = true;
	while (run) {
		for (int x = 0; x < board.width; x++) {
			for (int y = 0; y < board.height; y++) {
				tile_t tile = board_get_tile(&board, x, y);
				char sym = SYM_UNKNOWN;
				int col_f = COLOR_WHITE;
				int col_b = COLOR_BLACK;

				if (!(tile & TILE_TURNED)) {
					sym = SYM_UNTURNED;
					if (tile & TILE_FLAG) {
						col_b = COLOR_RED;
					}

				} else if (tile & TILE_MINE) {
					sym = SYM_MINE;
					col_f = COLOR_RED;

				} else {
					int adjmines = board_get_adjacent_mine_count(&board, x, y);
					if (adjmines > 0) {
						if (adjmines == 1) {
							col_f = COLOR_BLUE;
						} else {
							col_f = COLOR_YELLOW;
						}
						sym = '0' + adjmines;
					} else {
						sym = SYM_EMPTY;
					}
				}

				if (x == cur_x && y == cur_y) {
					if (col_b != COLOR_BLACK) {
						col_f = col_b;
					}
					col_b = COLOR_WHITE;
				}
				color_t col = util_color_get(col_f, col_b);
				attron(col);
				mvaddch(y, x, sym);
				attroff(col);
			}
		}

		color_t col_ok  = util_color_get(COLOR_GREEN, COLOR_BLACK);
		color_t col_bad = util_color_get(COLOR_RED, COLOR_BLACK);
		int row = 1;
		attron(col_ok);
		if (mode_nokill)            mvprintw(row++, board.width + 2, "testing");
		attroff(col_ok);
		attron(col_bad);
		if (mode_system)            mvprintw(row++, board.width + 2, "system");
		if (mode_initonly)          mvprintw(row++, board.width + 2, "hardcore");
		if (kill_signal == SIGKILL) mvprintw(row++, board.width + 2, "sigkill");
		attroff(col_bad);

		refresh();

		tile_t tile;
		switch (getch()) {
		case 'q':
			run = false;
			break;
		case 'h':
			cur_x--;
			if (cur_x < 0) cur_x = 0;
			break;
		case 'j':
			cur_y++;
			if (cur_y >= board.height) cur_y = board.height - 1;
			break;
		case 'k':
			cur_y--;
			if (cur_y < 0) cur_y = 0;
			break;
		case 'l':
			cur_x++;
			if (cur_x >= board.width) cur_x = board.width - 1;
			break;
		case 'f':
			board_toggle_flagged(&board, cur_x, cur_y);
			break;
		case 'x':
			tile = board_turn_tiles(&board, cur_x, cur_y);
			if (tile & TILE_MINE) {
				proc_t *proc = proc_get_random();
				if (!proc) {
					mvprintw(board.height + 1, 2, "Out of processes to kill!");
					break;
				}
				char *cmd;
				if (proc->cmdline) {
					cmd = proc->cmdline[0];
				} else {
					cmd = &proc->cmd[0];
				}
				erase();
				int col = util_color_get(COLOR_RED, COLOR_BLACK);
				char *msg;
				if (mode_nokill) {
					msg = "(Pretending)";
				} else {
					msg = "Too bad!";
				}
				attron(col);
				mvprintw(board.height + 1, 2, msg);
				mvprintw(board.height + 2, 2, "Killing %d, %s", proc->tgid, cmd);
				attroff(col);
				if (!mode_nokill) {
					proc_kill(proc, kill_signal);
				}
			}
			break;
		}
	}

	board_destroy(&board);
	cleanup();
	return 0;
}

void parse_cli(int argc, char **argv) {
	static struct option long_options[] = {
		{"hardcore", no_argument, 0, 'h'},
		{"test",     no_argument, 0, 't'},
		{"sigkill",  no_argument, 0, 'k'},
		{"system",   no_argument, 0, 's'},
	};
	int index = 0;
	int c;
	while ((c = getopt_long(argc, argv, "hpks", long_options, &index)) != -1) {
		switch (c) {
		case 'h':
			mode_initonly = 1;
			if (geteuid() != 0) {
				printf("The --hardcore option requires root priviliges!\n");
				exit(1);
			}
			break;
		case 't':
			mode_nokill = 1;
			break;
		case 'k':
			kill_signal = SIGKILL;
			break;
		case 's':
			mode_system = 1;
			if (geteuid() != 0) {
				printf("The --system option requires root priviliges!\n");
				exit(1);
			}
			break;
		}
	}
}

void init(void) {
	srand(time(NULL));
	proc_setfilter(procfilter);
	proc_init();
	initscr();
	if (!has_colors()) {
		fprintf(stderr, "Your terminal does not support colors :(\n");
		cleanup();
		exit(1);
	}
	start_color();
	util_init();
	noecho();
	raw();
}

void cleanup(void) {
	endwin();
	proc_cleanup();
}

char procfilter(proc_t *proc) {
	if (mode_system) {
		return 1;
	}
	if (mode_initonly) {
		return proc->tgid == 1;
	}
	return proc->euid == geteuid();
}
