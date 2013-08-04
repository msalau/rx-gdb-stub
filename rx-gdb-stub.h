#ifndef RX_GDB_STUB_H__
#define RX_GDB_STUB_H__

#ifdef __cplusplus
extern "C" {
#endif

void debug_puts (const char *str);
void stub_init (void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RX_GDB_STUB_H__ */
