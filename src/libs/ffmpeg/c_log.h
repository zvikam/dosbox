#ifdef __cplusplus
extern "C"
{
#endif

void C_LOG_MSG(char const* format,...) GCC_ATTRIBUTE(__format__(__printf__, 1, 2));
#define LOG_MSG C_LOG_MSG

#ifdef __cplusplus
} // extern "C"
#endif
