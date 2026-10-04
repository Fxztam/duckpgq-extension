import re, collections, sys

path = sys.argv[1]
lines = [l for l in open(path, encoding="utf-8", errors="replace") if " error " in l]
seen = set()
rows = []
pattern = re.compile(r"pgq-24f04ab[\\/](.*?)\((\d+),\d+\): error (C\d+): (.*)")
for line in lines:
    line = re.sub(r"\s*\[D:.*$", "", line.strip())
    if line in seen:
        continue
    seen.add(line)
    m = pattern.search(line)
    if m:
        rows.append((m.group(1), m.group(3), m.group(4)[:110], int(m.group(2))))
    else:
        rows.append(("?", "", line[:140], 0))

by_file = collections.Counter(r[0] for r in rows)
print(len(rows), "unique error lines in", len(by_file), "files")
for f, c in by_file.most_common(100):
    print("%4d %s" % (c, f))
print("---- top messages")
msgs = collections.Counter((r[1], r[2]) for r in rows)
for (code, msg), c in msgs.most_common(50):
    print("%4d %s %s" % (c, code, msg))
