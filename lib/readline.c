#include <inc/stdio.h>
#include <inc/error.h>
#include <inc/lib.h>
#include <inc/kbdreg.h>
#include <inc/string.h>

//static char buf[BUFLEN];

void readline(const char *prompt, char* buf)
{
	int i, c, echoing;
	int cursor_pos;  // Track cursor position separately from buffer length

	if (prompt != NULL)
		cprintf("%s", prompt);

	i = 0;
	cursor_pos = 0;
	echoing = iscons(0);
	
	// Clear the buffer
	memset(buf, 0, 10);
	
	while (1) {
		c = getchar();
		if (c < 0) {
			if (c != -E_EOF)
				cprintf("read error: %e\n", c);
			break;
		} else if (c >= ' ' && c <= '~' && i < 10-1) {
			// Printable character - insert at cursor position
			if (cursor_pos < i) {
				// Shift characters right to make room
				memmove(&buf[cursor_pos + 1], &buf[cursor_pos], i - cursor_pos);
			}
			buf[cursor_pos] = c;
			i++;
			
			if (echoing) {
				// Clear from cursor to old end, then reprint
				int old_end = i - 1; // before we incremented i
				for (int j = cursor_pos; j < old_end; j++)
					cputchar(' ');
				// Move cursor back to start position
				for (int j = cursor_pos; j < old_end; j++)
					cputchar('\b');
				// Print all characters from cursor to new end
				for (int j = cursor_pos; j < i; j++)
					cputchar(buf[j]);
				// Move cursor back to position after inserted char
				int chars_after = i - cursor_pos - 1;
				for (int j = 0; j < chars_after; j++)
					cputchar('\b');
			}
			cursor_pos++;
		} else if (c == '\b' && cursor_pos > 0) {
			// Backspace: move cursor left then delete
			cursor_pos--;
			if (cursor_pos < i) {
				// Shift characters left
				memmove(&buf[cursor_pos], &buf[cursor_pos + 1], i - cursor_pos);
			}
			i--;
			buf[i] = 0;
			
			if (echoing) {
				cputchar('\b');
				// Print from cursor to end
				for (int j = cursor_pos; j < i; j++)
					cputchar(buf[j]);
				// Print space to clear last position
				cputchar(' ');
				// Move cursor back to correct position
				int chars_to_move_back = i - cursor_pos + 1;
				for (int j = 0; j < chars_to_move_back; j++)
					cputchar('\b');
			}
		} else if (c == KEY_LF) {
			// Left arrow
			if (cursor_pos > 0 && echoing) {
				cursor_pos--;
				cputchar('\b');
			}
		} else if (c == KEY_RT) {
			// Right arrow
			if (cursor_pos < i && echoing) {
				cputchar(buf[cursor_pos]);
				cursor_pos++;
			}
		} else if (c == KEY_DEL) {
			// Delete key: remove char at cursor
			if (cursor_pos < i) {
				// Shift characters left
				memmove(&buf[cursor_pos], &buf[cursor_pos + 1], i - cursor_pos);
				i--;
				buf[i] = 0;
				
				if (echoing) {
					// Print from cursor to end
					for (int j = cursor_pos; j < i; j++)
						cputchar(buf[j]);
					// Print space to clear last position
					cputchar(' ');
					// Move cursor back to correct position
					int chars_to_move_back = i - cursor_pos + 1;
					for (int j = 0; j < chars_to_move_back; j++)
						cputchar('\b');
				}
			}
		} else if (c == KEY_UP || c == KEY_DN || c == KEY_HOME || c == KEY_END || 
		           c == KEY_PGUP || c == KEY_PGDN || c == KEY_INS) {
			// Ignore other special keys
			continue;
		} else if (c == '\n' || c == '\r') {
			if (echoing)
				cputchar('\n');
			buf[i] = 0;
			break;
		}
	}
}

void atomic_readline(const char *prompt, char* buf)
{
	sys_lock_cons();
	{
		int i, c, echoing;
		int cursor_pos;  // Track cursor position separately from buffer length

		if (prompt != NULL)
			cprintf("%s", prompt);

		i = 0;
		cursor_pos = 0;
		echoing = iscons(0);
		
		// Clear the buffer
		memset(buf, 0, BUFLEN);
		
		while (1) {
			c = getchar();
			if (c < 0) {
				if (c != -E_EOF)
					cprintf("read error: %e\n", c);
				break;
			} else if (c >= ' ' && c <= '~' && i < BUFLEN-1) {
				// Printable character - insert at cursor position
				if (cursor_pos < i) {
					// Shift characters right to make room
					memmove(&buf[cursor_pos + 1], &buf[cursor_pos], i - cursor_pos);
				}
				buf[cursor_pos] = c;
				i++;
				
				if (echoing) {
					// Clear from cursor to old end, then reprint
					int old_end = i - 1; // before we incremented i
					for (int j = cursor_pos; j < old_end; j++)
						cputchar(' ');
					// Move cursor back to start position
					for (int j = cursor_pos; j < old_end; j++)
						cputchar('\b');
					// Print all characters from cursor to new end
					for (int j = cursor_pos; j < i; j++)
						cputchar(buf[j]);
					// Move cursor back to position after inserted char
					int chars_after = i - cursor_pos - 1;
					for (int j = 0; j < chars_after; j++)
						cputchar('\b');
				}
				cursor_pos++;
			} else if (c == '\b' && cursor_pos > 0) {
				// Backspace: move cursor left then delete
				cursor_pos--;
				if (cursor_pos < i) {
					// Shift characters left
					memmove(&buf[cursor_pos], &buf[cursor_pos + 1], i - cursor_pos);
				}
				i--;
				buf[i] = 0;
				
				if (echoing) {
					cputchar('\b');
					// Print from cursor to end
					for (int j = cursor_pos; j < i; j++)
						cputchar(buf[j]);
					// Print space to clear last position
					cputchar(' ');
					// Move cursor back to correct position
					int chars_to_move_back = i - cursor_pos + 1;
					for (int j = 0; j < chars_to_move_back; j++)
						cputchar('\b');
				}
			} else if (c == KEY_LF) {
				// Left arrow
				if (cursor_pos > 0 && echoing) {
					cursor_pos--;
					cputchar('\b');
				}
			} else if (c == KEY_RT) {
				// Right arrow
				if (cursor_pos < i && echoing) {
					cputchar(buf[cursor_pos]);
					cursor_pos++;
				}
			} else if (c == KEY_DEL) {
				// Delete key: remove char at cursor
				if (cursor_pos < i) {
					// Shift characters left
					memmove(&buf[cursor_pos], &buf[cursor_pos + 1], i - cursor_pos);
					i--;
					buf[i] = 0;
					
					if (echoing) {
						// Print from cursor to end
						for (int j = cursor_pos; j < i; j++)
							cputchar(buf[j]);
						// Print space to clear last position
						cputchar(' ');
						// Move cursor back to correct position
						int chars_to_move_back = i - cursor_pos + 1;
						for (int j = 0; j < chars_to_move_back; j++)
							cputchar('\b');
					}
				}
			} else if (c == KEY_UP || c == KEY_DN || c == KEY_HOME || c == KEY_END || 
			           c == KEY_PGUP || c == KEY_PGDN || c == KEY_INS) {
				// Ignore other special keys
				continue;
			} else if (c == '\n' || c == '\r') {
				if (echoing)
					cputchar('\n');
				buf[i] = 0;
				break;
			}
		}
	}
	sys_unlock_cons();
}
