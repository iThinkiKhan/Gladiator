"""Bound missing SH2 command replies without modifying PlatformIO's library cache.
The Adafruit BNO08x dependency is pinned to 1.2.5 in platformio.ini.
"""
from pathlib import Path

def bounded_source(source):
    old = """    // While op not complete and not timed out.
    while ((pSh2->pOp != 0) &&
           ((pOp->timeout_us == 0) ||
            ((now_us-start_us) < pOp->timeout_us))) {"""
    new = """    // Missing peripheral replies must return an error, not hang the worker.
    // Preserve operation-specific deadlines; bound formerly unlimited waits.
    const uint32_t timeout_us = pOp->timeout_us ? pOp->timeout_us : 500000;
    while ((pSh2->pOp != 0) && ((now_us-start_us) < timeout_us)) {"""
    if source.count(old) != 1:
        raise RuntimeError("Adafruit SH2 source changed: review command timeout patch")
    result = source.replace(old, new)
    opening = """    // Clear everything in sh2 structure.
    memset(&_sh2, 0, sizeof(_sh2));"""
    reopening = """    // Release the existing transport before rediscovery, including failed init.
    // Otherwise the fixed SHTP instance pool is exhausted by retries.
    if (_sh2.pShtp) sh2_close();
    // Clear everything in sh2 structure.
    memset(&_sh2, 0, sizeof(_sh2));"""
    if result.count(opening) != 1:
        raise RuntimeError("Adafruit SH2 open changed: review transport cleanup")
    return result.replace(opening, reopening)


def bounded_transport(source):
    old = """    cursor = 0;
    remaining = len;
    while (remaining > 0) {"""
    new = """    cursor = 0;
    remaining = len;
    // One deadline for the cargo, including repeated HAL write failures.
    const uint32_t start_us = pShtp->pHal->getTimeUs(pShtp->pHal);
    while (remaining > 0) {"""
    retry = """        while (status == 0)
        {
            shtp_service(pShtp);"""
    bounded = """        while (status == 0)
        {
            if ((uint32_t)(pShtp->pHal->getTimeUs(pShtp->pHal) - start_us) >= 500000) {
                status = SH2_ERR_TIMEOUT;
                break;
            }
            shtp_service(pShtp);"""
    if source.count(old) != 1 or source.count(retry) != 1:
        raise RuntimeError("Adafruit SHTP source changed: review write timeout patch")
    return source.replace(old, new).replace(retry, bounded)

try:
    Import("env")
except NameError:
    env = None
if env is not None:
    def bounded_sh2(build_env, node):
        source = Path(node.srcnode().get_abspath())
        if source.name not in ("sh2.c", "shtp.c") or source.parent.parent.name != "Adafruit BNO08x":
            return node
        target = Path(build_env.subst("$BUILD_DIR")) / "bounded-sh2" / source.name
        patch = bounded_source if source.name == "sh2.c" else bounded_transport
        text = patch(source.read_text(encoding="utf-8"))
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_text(encoding="utf-8") != text:
            target.write_text(text, encoding="utf-8")
        print(source.name + " missing-reply/write deadline: 500 ms (library cache unchanged)")
        return build_env.File(str(target))
    env.AddBuildMiddleware(bounded_sh2)
