/**
 * @file  init_stubs.c —— newlib 启动桩（配合 -nostartfiles）
 *
 * 官方 startup_stm32g431xx.s 在跳进 main 之前会调用 __libc_init_array()，
 * 它会遍历 .preinit_array / .init_array（C++ 全局构造、__attribute__((constructor))
 * 等），最后调用 _init()。
 *
 * 本工程链接时用了 -nostartfiles（不使用 GCC 自带的 crt0/crti/crtn），
 * 因此 newlib 里 crti.o 提供的 _init/_fini 不会被链进来，链接器会报：
 *     undefined reference to `_init'
 *
 * 这里给出空的 _init/_fini：C 工程没有需要额外初始化的运行环境，
 * .init_array 仍会被 __libc_init_array 正常遍历，功能不受影响。
 * 若以后加入 C++ 全局对象，也不需要改这里。
 */

void _init(void)
{
}

void _fini(void)
{
}
