# Quick check: is the open program analyzed? Report function count and defined
# string count. Zero functions => imported but not auto-analyzed.
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()

nfun = fm.getFunctionCount()

nstr = 0
for data in listing.getDefinedData(True):
    if data.hasStringValue():
        nstr += 1
        if nstr >= 5: break

print("ANALYSIS-PROBE: functions=%d  defined-strings(>=5?)=%d" % (nfun, nstr))
