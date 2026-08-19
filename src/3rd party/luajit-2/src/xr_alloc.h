#ifndef _XR_POOL_H
#define _XR_POOL_H

// В сталкере x64 беда с luajit`ом - из-за того, что луаджит непременно требуется память из младших адресов,
// на больших локациях луаджит не может выделить память, так как она уже занята под другие ресурсы игры.
// Как вариант попробую выделить большой кусок памяти(128МБ) в начале игры и буду его потихоньку выдавать луаджиту

void XR_INIT();
void* XR_MMAP(size_t size);
void XR_DESTROY(void* p, size_t size);

void XR_EARLY_INIT();

// Balloon campaign step 1: describe the reserved arena once, from a place where the log exists.
// XR_INIT runs at the top of WinMain, before xrCore is initialised, so it cannot report itself.
void XR_ARENA_REPORT();

#endif