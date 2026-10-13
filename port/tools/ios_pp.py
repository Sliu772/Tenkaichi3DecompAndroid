"""Text fixes for building the engine on Apple platforms (used by android.py with BT3_PLATFORM=ios).

strip_availability(text)  preprocessed C -> the same without __attribute__((availability(...))).
    Apple's headers mark nearly everything with version numbers (`introduced=7.0`). eeconst.py rewrites every decimal
    constant of the preprocessed text into a hexadecimal float, which turns 7.0 into 0x1.cp+2 and makes clang refuse the
    attribute ("expected a version of the form 'major[.minor[.subminor]]'"). The game never needs availability checks,
    so they are removed before eeconst runs.

macho_asm(text)           the GNU/ELF assembler text of port/data/*.s -> Mach-O's:
    sections (.data/.bss/.rodata...) all become __DATA,__data (the game's tables are filled in at start, so even the
    read-only ones have to be writable), and every C-visible symbol gets the leading underscore Mach-O puts on them.
"""
import re

_ATTR = re.compile(r"__attribute__\s*\(\(")
_AVAIL = re.compile(r"\s*(?:__)?availability(?:__)?\s*\(")


def _skip_string(s, i):
    q = s[i]
    i += 1
    while i < len(s) and s[i] != q:
        i += 2 if s[i] == "\\" else 1
    return i + 1


def _match_paren(s, i):
    """s[i] == '(' -> index just after its matching ')' (string aware)."""
    depth = 0
    while i < len(s):
        c = s[i]
        if c in "\"'":
            i = _skip_string(s, i)
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def _split_top(s):
    """Items of an attribute list, split at commas outside parentheses and strings."""
    out, depth, start, i = [], 0, 0, 0
    while i < len(s):
        c = s[i]
        if c in "\"'":
            i = _skip_string(s, i)
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            out.append(s[start:i])
            start = i + 1
        i += 1
    out.append(s[start:])
    return out


def strip_availability(text):
    if "availability" not in text:
        return text
    out, pos = [], 0
    for m in _ATTR.finditer(text):
        if m.start() < pos:
            continue
        outer_open = m.end() - 2                    # the first '(' of '(('
        end = _match_paren(text, outer_open)
        if end < 0:
            continue
        inner = text[outer_open + 2 : end - 2]      # between '((' and '))'
        if text[end - 2 : end] != "))" or "availability" not in inner:
            continue
        keep = [it for it in _split_top(inner) if not _AVAIL.match(it)]
        out.append(text[pos : m.start()])
        if keep:
            out.append("__attribute__((" + ",".join(keep) + "))")
        pos = end
    out.append(text[pos:])
    return "".join(out)


_SECTION = re.compile(r"^\s*\.section\s+\.(?:data|bss|rodata|sdata|sbss)\b.*$")
_GLOBL = re.compile(r"^(\s*\.globl\s+)(\S+)\s*$")
_LABEL = re.compile(r"^([A-Za-z_$][\w.$]*):\s*$")
_LONG = re.compile(r"^(\s*\.(?:long|word|4byte)\s+)([A-Za-z_$][\w.$]*)(.*)$")


def macho_asm(text):
    out = []
    for line in text.splitlines():
        if _SECTION.match(line):
            out.append(".section __DATA,__data")
            continue
        m = _GLOBL.match(line)
        if m:
            out.append(f"{m.group(1)}_{m.group(2)}")
            continue
        m = _LABEL.match(line)
        if m:
            out.append(f"_{m.group(1)}:")
            continue
        m = _LONG.match(line)
        if m:
            # a 4-byte pointer in a table = offset from the image header (p32rebase.py): the linker resolves the difference
            out.append(f"{m.group(1)}_{m.group(2)}{m.group(3)} - __mh_execute_header")
            continue
        out.append(line)
    return "\n".join(out) + "\n"
