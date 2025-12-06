# Summary: Fixing Command Line Deletion in the FOS Kernel

## The Initial Problem

You reported that when using DELETE or BACKSPACE keys in the FOS command prompt, characters would be removed from the buffer correctly, but the GUI would show "empty nodes" or "gaps" - the deleted characters visually remained on screen as blank spaces that you could navigate through.

## Key Discovery #1: The Wrong File

Initially, I was looking at `lib/readline.c`, but I discovered that the FOS command prompt actually uses **`command_prompt_readline()`** in **`kern/cmd/command_readline.c`**. This was the actual file that needed fixing.

## The Bugs I Found (in order of discovery)

### 1. DELETE Key Issues

- **No screen redraw**: The buffer was shifted correctly, but nothing updated the display
- **Wrong condition**: Used `i > 0` instead of `i < lastIndex`, preventing deletion at start of line
- **Fall-through bug**: When `i >= lastIndex`, it fell through to the printable character block and printed garbage (the keycode 0xE9 as a character)

### 2. BACKSPACE Key Issues

- **Broken shift loop**: Only moved ONE character instead of shifting the entire string:

  ```c
  for (int var = i; var <= i; ++var)  // WRONG: only loops once!
  ```

- **No lastIndex update**: Forgot to decrement `lastIndex`, causing length tracking to drift

- **No screen redraw**: Same as DELETE - buffer correct, screen wrong

### 3. Mid-Line Editing Logic

- **Incorrect lastIndex increment**: The code blindly did `lastIndex++` on every keystroke, breaking overwrite mode

### 4. Uninitialized Buffer

- The buffer contained garbage from stack memory, which caused unpredictable behavior when shifting

### 5. The CRITICAL Bug: Destructive Backspace

This was the **smoking gun** that explained ALL the visual glitches:

In `kern/cons/console.c`, the `\b` (backspace) character is implemented as **destructive**:

```c
case '\b':
    if (crt_pos > 0) {
        crt_pos--;
        crt_buf[crt_pos] = (c & ~0xff) | ' ';  // WRITES A SPACE!
    }
```

I was using `cputchar('\b')` to move the cursor back after redrawing, which **erased the text I just printed**!

Meanwhile, `KEY_LF` (Left Arrow, keycode 228) is **non-destructive**:

```c
case KEY_LF:
    if(crt_pos>0)
        crt_pos--;  // JUST MOVES, DOESN'T ERASE
```

## The Complete Fix

I applied these changes to `kern/cmd/command_readline.c`:

### 1. DELETE Key (0xE9) - Lines 224-250

```c
else if (c == 0xE9) {  // Exclusive block - no fall-through
    if (i < lastIndex) {  // Correct condition
        // Shift buffer left
        for (int var = i; var <= lastIndex; ++var)
            buf[var] = buf[var + 1];

        lastIndex--;
        buf[lastIndex] = 0;  // Null-terminate

        // Redraw screen
        if (echoing) {
            for (int j = i; j < lastIndex; j++)
                cputchar(buf[j]);
            cputchar(' ');  // Clear last position

            // NON-DESTRUCTIVE cursor movement
            int chars_to_move_back = lastIndex - i + 1;
            for (int j = 0; j < chars_to_move_back; j++)
                cputchar(228);  // Left Arrow, not '\b'!
        }
    }
}
```

### 2. BACKSPACE Key ('\b') - Lines 254-278

```c
else if (c == '\b' && i > 0) {
    if (echoing)
        cputchar(c);  // Destructive backspace to erase char before cursor

    // CORRECTED shift loop
    for (int var = i; var <= lastIndex; ++var)
        buf[var - 1] = buf[var];

    i--;
    lastIndex--;  // ADDED: decrement length
    buf[lastIndex] = 0;

    // Redraw screen
    if (echoing) {
        for (int j = i; j < lastIndex; j++)
            cputchar(buf[j]);
        cputchar(' ');

        // NON-DESTRUCTIVE cursor movement
        int chars_to_move_back = lastIndex - i + 1;
        for (int j = 0; j < chars_to_move_back; j++)
            cputchar(228);  // Left Arrow, not '\b'!
    }
}
```

### 3. Typing Characters - Lines 246-250

```c
else if (c >= ' ' && i < BUFLEN - 1 && c != 229 && c != 228) {
    if (echoing)
        cputchar(c);
    buf[i++] = c;
    if (i > lastIndex)  // ADDED: update length correctly
        lastIndex = i;
}
```

### 4. Buffer Initialization - Line 83

```c
memset(buf, 0, BUFLEN);  // ADDED: zero out garbage
```

## How It Works Now

1. **When you press DELETE**:

   - Checks if cursor is before end (`i < lastIndex`)
   - Shifts all characters from cursor position left by 1
   - Decrements `lastIndex` and null-terminates
   - Prints the shifted characters to screen
   - Prints a space to clear the old last character
   - Moves cursor back using **non-destructive Left Arrow (228)** - text stays visible!

2. **When you press BACKSPACE**:

   - Moves cursor back with destructive `\b` (erases previous char visually)
   - Shifts buffer left to remove the character
   - Redraws remaining text
   - Moves cursor back using **non-destructive Left Arrow (228)**

3. **Visual and Buffer State Stay Synced**:
   - `i` tracks cursor position
   - `lastIndex` tracks string length
   - `buf` contains the actual text
   - Screen always shows exactly what's in `buf[0...lastIndex-1]`

## The Key Insight

The entire problem boiled down to using **`\b` (destructive) instead of `KEY_LF` (non-destructive)** for cursor repositioning. I was literally printing the correct text and then immediately erasing it as I moved the cursor back!

By switching to `cputchar(228)` for cursor movement in the redraw logic, the text remains visible and the display matches the buffer perfectly.

## Update: Insert Mode Implementation

I implemented insert mode to prevent characters from being overwritten when typing in the middle of a command.

### The Problem

Previously, typing in the middle of a string would overwrite the existing character at that position instead of inserting.

### The Change

Modified the typing logic to shift existing text to the right before inserting the new character.

```c
else if (c >= ' ' && i < BUFLEN - 1 && c != 229 && c != 228) {

    // Shift characters right if we are in the middle of the string
    if (i < lastIndex) {
        memmove(&buf[i + 1], &buf[i], lastIndex - i);
    }

    buf[i] = c;
    lastIndex++;      // Increment length
    buf[lastIndex] = 0; // Null-terminate

    if (echoing) {
        // Print current char and all subsequent chars to update screen
        for (int j = i; j < lastIndex; j++)
            cputchar(buf[j]);

        // Move cursor back to the position AFTER the inserted character
        // visual cursor is at invalid position (end of string), move it back
        int chars_to_move_back = lastIndex - (i + 1);
        for (int j = 0; j < chars_to_move_back; j++)
            cputchar(228);
    }
    i++; // Move logical cursor
}
```
