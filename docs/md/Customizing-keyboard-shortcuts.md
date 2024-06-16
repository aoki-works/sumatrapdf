# Customizing keyboard shortcuts

**Available in version 3.4 or later.**

You can add new keyboard shortcuts or re-assign existing shortcut to a different [command](Commands.md).

To customize keyboard shortcuts:

- use `Settings` / `Advanced Options...` menu (or `Ctrl + K` to invoke Command Palette, type `adv` to narrow down and select `Advanced Options...` command)
- this opens a notepad with advanced settings file
- find `Shortcuts` array and add new shortcut definitions

An example of customization:

```
Shortcuts [
	[
		Cmd = CmdOpenFolder
		Key = Alt + O
	]
	[
		Cmd = CmdOpen
		Key = x
	]
	[
		Cmd = CmdNone
		Key = q
	]
]
```

Explanation:

- we added `Alt + O` keyboard shortcut for `CmdOpenFolder` command. It opens a folder for browsing.
- by default SumatraPDF has `Ctrl + O` shortcut for `CmdOpen` (open a file) command. This changes the shortcut to `x`
- by default `q` closes the document. By binding it to `CmdNone` we can disable a built-in shortcut

## **Format of `Key` section:**

- just a key (like `a`, `Z`, `5`) i.e. letters `a` to `z`, `A` to `Z`, and numbers `0` to `9`
- modifiers + key. Modifiers are: `Shift`, `Alt`, `Ctrl` e.g. `Alt + F1`, `Ctrl + Shift + Y`
- there are some special keys (e.g. `Alt + F3`)
    - `F1` - `F24`
    - `numpad0` - `numpad9` : `0` to `9` but on a numerical keyboard
    - `Delete`, `Backspace`, `Insert`, `Home`, `End`, `Escape`
    - `Left`, `Right`, `Up`, `Down` for arrow keys
    - full list of [special keys](https://github.com/sumatrapdfreader/sumatrapdf/blob/master/src/Accelerators.cpp#L14)
- without modifiers, case do matter i.e. `a` and `A` are different
- with modifiers, use `Shift` to select upper-case i.e. `Alt + a` is the same as `Alt + A` , use `Alt + Shift + A` to select the upper-case `A`

## **Commands**

You can see a [full list of commands](Commands.md) ([or in the source code](https://github.com/sumatrapdfreader/sumatrapdf/blob/master/src/Commands.h#L9))

## **Notes**

The changes are applied right after you save settings file so that you can test changes without restarting SumatraPDF.

There is no indication if you make a mistake (i.e. use invalid command or invalid syntax for `Key`).

If the shortcut doesn’t work, double-check command name and keyboard shortcut syntax.