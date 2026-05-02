## Superpowers 输出路径配置
| 产物类型 | 自定义路径 |
|---|---|
| Design specs（设计文档） | docs/design/ |
| Execution plans（实施计划） | docs/plans/ |

IMPORTANT: Design specs MUST be saved to docs/design/, NOT docs/superpowers/specs/.

## 其他项目的路径

请参考GEM5对CHI协议的实现以及CHI协议的spec，实现我们自己的CHI model.

 - GEM5源码的路径: ./gem5
 - ARM CHI的spec文档路径: ./docs/CHI/IHI0050H_amba_chi_architecture_spec.pdf

 ## GEM5编译

编译 gem5 请优先用增量编译：gem5 的 SCons 默认支持增量，日常改代码直接重复执行 scons build/xxx/gem5.opt -jN 就行，不用清理、不删 build 目录；只有改构建脚本或缓存异常时才全量重编，常规开发一律走增量。

## 测试

测试放在 tests 目录