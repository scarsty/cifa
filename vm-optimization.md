# VM 优化记录

## 性能优化历史摘要：约 300ms 到当前 22ms

下表汇总已保留的优化、关键版本快照及编译器对照。`阶段耗时`是当时可执行版本的 Release PI
`execute_ms` 中位数或同批代表值，用于展示演进位置；不同日期、机器、二进制和 benchmark harness 不能直接
相减。`同批证据`列明确写有 A/B、轮转或同版本交错的，才可据此认定该项提速；“阶段记录”只说明当时实际
耗时，不归因给相邻改动。所有 PI 行均以脚本输出校验为前提，早期记录为 length=502/FNV 校验，近期记录为
502 字符精确断言。

| 阶段耗时 | 最终保留的主要优化 | 同批证据或记录结论 |
| ---: | --- | --- |
| `313.109 ms` | 早期常量折叠、数值内建直达、局部一维数组直接路径 | 同进程未优化 `355.710 ms` -> 优化 `313.109 ms`，约快 `11.98%` |
| `228.504 ms` | 局部初始化直接建槽、double-to-int 受限快速写入 | 当前批绝对值；未做有效交错 A/B，不单独归因 |
| `153.845 ms` | 槽执行迁移，数值运算/调用/返回减少 Object 往返 | 当前批绝对值；跨批 `199.572 ms` 仅作阶段记录 |
| `122.198 ms` | 动态 scope、范围、方法参数及返回状态迁入槽 | 当前批绝对值；跨批不作严格收益结论 |
| `108.370 ms` | 默认优化、函数冻结和旧 Object 双轨删除 | 同批未优化 `172.656 ms`，优化 `108.370 ms` |
| `95.837 ms` | Scope 静态 binding 预留与纯静态 Scope 容器复用 | `109.983` -> `102.063 ms`（`-7.2%`），再 `101.450` -> `96.867 ms`（`-4.5%`）；最终复测 `95.837 ms` |
| `94.908 ms` | Module 级 int/double 名称 ID 与 NumericBinding | A/B/A/B/A/B：`96.860` -> `94.908 ms`，约快 `2.02%` |
| `91.858 ms` | const 数组导入避免先复制 ObjectVector | A/B/B/A：`95.604` -> `91.858 ms`，约快 `3.92%` |
| `88.9 ms` | 执行期唯一权威全局槽，减少宿主边界数组转换 | 两批中位数 `88.39/89.43 ms`；相对前一候选仅作跨批阶段记录 |
| `85--78 ms` | VmArray/VmMap COW、只读索引修正、寄存器原生内建调用 | 多批输出正确；环境漂移或未冻结基线，不单独宣称时间收益 |
| `63.096 ms` | 执行器由平行 opcode `if` 改为完整 `switch/case` | 四批 `62.6468/63.6289/63.7821/62.3279 ms`；相对约 `78.293 ms` 为同口径跨版本记录 |
| `57.046 ms` | `NumericBinary` 16B 热数值微指令 | A/B/B/A/A/B：`62.986` -> `57.046 ms`，约快 `9.43%` |
| `56.553 ms` | `NumericForNext` 融合整数更新与回跳 | A/B/B/A/A/B：`57.282` -> `56.553 ms`，约快 `1.27%` |
| `53.144 ms` | `NumericCompareBranch` 融合数值比较与分支 | A/B/B/A/A/B：`57.441` -> `53.144 ms`，约快 `7.48%` |
| `49.64 ms` | `ArrayPushGlobal` 端到端全局数组追加 | A/B/B/A/A/B：`53.53` -> `49.64 ms`，约快 `7.3%` |
| `48.48 ms` | `LoadLocal + ArrayPushGlobal` 融合 | 复测 A/B/B/A：`49.49` -> `48.48 ms`，约快 `2.0%` |
| `44.59 ms` | `LoadLocal + Index` 的 `IndexLocal` | 交错 A/B：约 `48.75` -> `44.59 ms`，约快 `8.5%` |
| `43.28 ms` | 二元数学调用的双局部操作数描述符 | 两轮 A/B：约 `44.54` -> `43.28 ms`，约快 `2.8%` |
| `42.48 ms` | `ConstantLocal` 直接写目标局部槽 | A/B/B/A/A/B：`43.51` -> `42.48 ms`，约快 `2.37%` |
| `39.216/39.330 ms` | 显式 double 目标槽直写 | A/B/A：`41.442` -> `39.216/39.330 ms`，约快 `5.3%` |
| `41.0412 ms` | 标准 PMR、栈上临时存储迁移 | 独立阶段记录：同机 MSVC Release PI `43.5414` -> `41.0412 ms`（约 `-5.7%`）；这是整次迁移结果，不能单独归因于栈缓冲区。 |
| `36.33 ms` | 复合赋值作用域分析与整数/浮点 fast path | 阶段记录：`incrementf` 从 `182.95` 到 `9.49 ms`；百万次 `total += i` 独立 probe 从 `33.55` 到 `10.49 ms`。PI 只有修复后实测值，未做该项冻结 A/B。 |
| `29.95 ms` | 循环体变量声明提升与已知局部保留 | 2026-09-20，`a4f2a93` 对当前、7 轮轮转：PI `36.06` -> `29.95 ms`（约 `-17%`）；calls `22.61` -> `21.77 ms`，increment `8.47` -> `7.74 ms`，incrementf `184.21` -> `9.75 ms`。 |
| `29.95 ms` | 放开 handler 强制 noinline | 2026-09-20，MSVC 7 轮交替：increment `-6%`、incrementf `-4%`、calls `-2%`，PI 与 strings 持平；不计为 PI 收益。 |
| `25.27 / 23.51 ms` | clang-cl inline budget 对照 | 2026-09-22，Clang 22.1.3、各 21 PI 样本：默认 / 高 inline threshold，中位数 `25.27` -> `23.51 ms`（约 `-6.95%`）；同源码、同机、仅改变 inline 阈值。 |
| `22.38 / 20.00 / 16.20 ms` | 协议重构快照相对 `HEAD` | 2026-09-23，3 轮交错、每轮 21 PI 样本的均值：MSVC `24.40` -> `22.38 ms`（`-8.3%`）、clang-cl `21.22` -> `20.00 ms`（`-5.5%`）、高 inline clang `17.79` -> `16.20 ms`（`-9.0%`）。覆盖完整协议重构集，不能归因到单个 opcode。 |
| `22.5446 ms` | 当前静态 CFG / Method / Range / verify 优化后的 MSVC PI | 2026-09-23，MSVC v145 `/O2 /DNDEBUG /MD`，21 个样本；均值 `22.5644 ms`，范围 `22.2952--22.8785 ms`，PI 返回 502 字符。不同重构阶段无冻结 A/B，作为当前测量记录，不单独归因。 |

注：2026-09-15 的 dispatcher handler 拆分是在另一台 CPU 上测量的，而且基线已经包含 `2ab6301`（allocator
和省掉不必要的 scoped guard）等改动。因此，不能把这组结果和上表的数字跨机器直接相减；独立结果见文末
`## 2026-09-15：clang-cl inline budget 实验与 dispatcher handler 拆分（独立测机）`。

未列入的候选要么已撤回，要么没有稳定加速：例如局部赋值 copy-to-move、临时槽深度复用、原生内建调用、
冷热诊断分离。它们的完整样本和原因仍在后文，以避免“最终约 40ms”掩盖负实验或把环境波动误记为优化收益。

## 2026-09-23：静态生命周期与控制协议重构

近期两组改动的共同原则是：词法层级、局部槽、清理边、控制协议编号和诊断范围均由编译器计算；VM 只保留
语言可观察的值、容器快照和错误语义。它们改变了字节码 ABI 和执行器结构，当前应以 Cifa/CifaBytecode
黑盒一致性验收，不能把不同提交、不同编译器的绝对耗时倒推出某一项的独立收益。

### 第一组：静态局部槽与显式清理

- 取消运行期 Scope/ScopeStack、动态局部绑定、别名解析和通用 unwind。参数与局部变量在 lowering 时取得固定
	槽号；局部 opcode 直接访问当前函数的 `active_locals`，普通全局 opcode 只访问 `global_values`。
- 编译器在普通块尾、`break`、`continue`、`goto` 和 `return` 的控制流边上发射精确的 `ReleaseLocal`；Range 与
	Switch 的结束也作为静态 control-cleanup 边处理。诊断范围迁入 `DiagnosticFrameEvent` 冷表，`verify()` 预先
	展开每个 PC 的诊断帧，`compact()` 与代码同步重映射。
- `ReleaseLocal` 不能按“数值变量生命周期已知”而直接删除。`release_scope_slot()` 虽只析构字符串、数组、map
	和 `any` payload，但还会清除名称、类型、数值 binding 与参数来源；调用窗口复用时遗漏这些元数据会使无类型
	参数或后续声明错误继承旧 `int`/`double` 约束。因此它是静态已知的清理动作，不是可省略的运行期守护。
- 已通过 Debug Cifa/CifaBytecode 黑盒回归及运行时报错/恢复对照；该组重构没有冻结的同批 A/B，不能单独声称
	性能收益。

### 第二组：Method、Range、Switch 协议收敛

- Method 收敛为 `MethodCheck`、`MethodCall` 和单参数 `MethodPush`；Range 使用 begin/next/end 三阶段；Switch
	使用 mark/active/default/compare/end 五阶段。旧的 begin/value/end 等平行 opcode 与运行期守护状态已删除。
- `Range` 仍必须在入口创建数组快照：AST 语义规定循环体修改原数组时，迭代序列保持入口时的值副本。它的
	index、快照槽和结束释放不能由“循环形状静态”推导为零成本。
- `Switch` 的 condition 也必须保留一次求值和与 case 比较的值语义；不过“已经命中则后续语句直落、尚未命中
	则继续寻找”的 `active` 布尔状态是编译器已知控制流的编码冗余，见下一节的首要候选。
- 当前树的 Debug 回归为 76/76；Release 对 `HEAD` 的交错测量中，PI 在 MSVC、clang-cl 默认和 clang-cl 高
	inline 下均更快，百万次整数循环未见回退。但这些测量覆盖整个重构集，不能归因到某个协议 opcode。

### 下一轮候选（先证明，再实现）

1. **Switch 静态 CFG 降低优先级最高。** 当前每个 case 前或 case 体语句前都会执行 `Switch(active)` 并经
	 `Branch` 决定是否跳过。lowering 可为每个 case 生成“未命中继续比较”的跳转，并在首次命中或 default 后
	 直接落入后续语句；只保留 condition 槽和 case 比较。必须保留 case 表达式的从左到右求值、default 位置、
	 fallthrough、break/continue/goto 的既有 cleanup 边。先加 profile 统计 `Switch` 各阶段动态次数，再仅用含
	 多 case 和 fallthrough 的脚本做冻结 A/B。
2. **全局 receiver 的 method 直接链接。** `ArrayPushGlobal` 已用编译期 `base_name_id` 在执行期缓存 global slot；
	 `MethodCheck`/`MethodCall` 仍调用 `named_value(name)` 做通用名称解析。对编译期可判定的全局 receiver，可让
	 CallSite 保存已链接 global slot 的访问类别并直接读 `global_values`，同时保留宿主重入、跨脚本变量更新和
	 typed array/map 的转换/错误路径。不得把函数局部同名变量误优化成全局 receiver。
3. **静态局部 Range 绑定直写。** `Range next` 已知局部槽时仍先把元素放进 scratch，再调用通用
	 `bind_range()`，之后 move 到局部槽。可为无类型或已验证类型的局部 range 变量建立专用路径，直接从 snapshot
	 元素写目标槽；带 `auto` 推断、显式转换、未初始化诊断或任何可观察 metadata 的路径仍回退。先对照 AST 的
	 数组修改快照、typed 元素转换和循环体遮蔽脚本。
4. **编译期不可达块删除。** 现有 `compact()` 只删除显式 `Removed`。在 `verify()` 已得到 CFG 与寄存器状态后，
	 可标记从 root entries/函数入口不可达的 PC 并同步清理 diagnostics、frame events 和跳转 remap。此项首先
	 降低代码和冷表体积，只有脚本含 goto/常量分支产生死块时才可能减少执行前缓存压力；不应预设 PI 收益。

不建议重试的方向：删除 Range 快照、删除 `ReleaseLocal`、恢复动态 Scope guard、或把相邻 opcode 机械拼成
大 handler。前两者破坏语言语义，后两者已证明会增加 dispatcher 体积或恢复不必要的运行期状态。

### 已实施：静态 CFG、直接槽和 verify 状态收缩

- Switch lowering 已改为静态 case 测试链：每个 case 表达式只在此前未命中时按源码顺序求值，命中后跳入对应
	body；所有 body 在源码顺序中自然 fallthrough。default 仅是全部 case 未命中的落点。运行期不再为每条 case
	body 语句执行 `Switch(active)` 与 `Branch`，`SwitchState::active`、active/default 阶段也随之删除；条件槽和
	case equality 仍保留。
- `MethodPush`、`MethodCheck`、`MethodCall` 对编译期确认的全局 receiver 复用 `linked_global()` 取得的稳定槽，
	不再每次调用 `named_value(name)` 进行全局名称查找。不存在的 receiver 仍按旧规则经 `assign_named()` 创建，
	局部 receiver、类型转换和错误顺序保持原路径。
- Range 的循环变量已有静态 local slot 时，next 阶段直接把快照元素写入 local slot 并调用同一 `bind_range()`；
	删除 scratch 值的 move 中转，但保留快照、索引、`auto`/显式类型绑定及错误位置。
- `verify()` 的 `methods` CFG 状态从未由任何 opcode 写入，只扩大了每个待验证 PC 的状态复制与 merge 比较，现已
	删除；Call、Range、Switch 的真实结构状态仍完整验证。
- Debug x64 Cifa/CifaBytecode 黑盒回归通过 `76/76`。当前 MSVC Release 基准同时验证 PI 返回 502 字符，百万次
	increment 返回 `1000000`；本轮尚未冻结本次改动前的同机 A/B，不能把 `22.5446 ms` 归因给其中任意一项。

## 完整优化计划（2026-09-13）

状态：2026-09-13 已撤销 I/D/B/A 四区负载池，恢复每槽单一内联 `BytecodeValue::Storage`。
以下计划已按 Lua 5.4 架构对照重新排序；历史 IDBA 记录仅说明曾实现和测量过，不再代表当前方向。
当前槽类型已由 RegisterFile 更名为 RegisterSlots；下方历史记录保留当时名称。

### 总体目标与约束

- 采用统一连续槽窗口；每个槽直接保存 tag/payload，不再通过 I/D/B/A 四个池二次寻址。
- 参数、局部变量、临时值使用同一套槽存储；禁止每次脚本调用另建参数或局部槽数组。
- Object 只在传统 Cifa/AST 宿主 API 和公开结果边界转换；CifaBytecode native 回调直接访问受控寄存器上下文。
- 保持动态类型、NoValue 传播、未初始化检查、类型转换、容器深复制和宿主重入语义。
- 不新增循环、跳转或调用深度限制，不以异常驱动普通运行时错误控制流。
- 优化记录统一写入本文档，不写入 README。

### 编译器重构期验证与性能策略（2026-09-14）

- 当前目标是完成 Lua 式编译器/字节码协议重构，不在中途用 PI 或 Lua 对比宣称性能结论；重构前后的
	opcode、寄存器布局、控制流和调用协议尚未冻结，任何局部样本都没有解释力。
- 微小且可局部推理的编译器改动只运行对应结构断言或最小脚本探针。完整 Debug 回归只在一个完整协议切片
	完成时、涉及控制流/调用/左值写入等高风险边界前后，以及重构阶段结束前运行。
- Release benchmark 只在字节码格式和主要发射路径冻结后恢复，届时先冻结基线，再以交错 A/B 验证；不把
	编译成功、单次运行或指令数减少当作性能证据。
- 正式回归只验收 Cifa 可观察语义，以及 CifaBytecode 公开的宿主 ABI（`NativeCallContext`、标签入口、
	宿主重入与错误传播）。不将 opcode 出现次数、指令尺寸/统计、寄存器窗口、`CompactValue`/容器布局或
	融合路径作为门禁；这些实现细节可以随重构替换，不能反向限制语言演进。

#### 类别化 store 协议补充（2026-09-14）

- 无类型脚本函数参数只接收调用者的值；进入函数窗口后必须清除调用者的静态类型与数值绑定，避免动态
	参数赋值被错误地按调用者的 `int`/`double` 约束转换。
- `PrepareGlobalStore`、`PrepareFieldStore`、`PrepareIndexStore` 的冻结目标按表达式嵌套保存。逗号表达式
	或其他右值中的内层写入可临时压入并消费自己的目标，外层 `SetGlobal`/`SetField`/`SetIndex` 随后恢复消费
	原目标；单一 prepared-store 状态会使外层写入错误地报告无准备目标。

### 1. 统一 tagged value 槽布局

- [x] 每个 `BytecodeValue` 直接内联保存 `variant<monostate, int64_t, double, bool, any>`。
- [x] 删除 I/D/B/A 四个负载池、区域偏移、资源 `unique_ptr` 和四组空闲偏移链。
- [ ] 后续若缩小槽，必须使用单一 tagged value 表示；不得再次引入按类型分散负载和二次寻址。
- [x] 动态数值直接保存在槽内 variant，不装进 `std::any`。
- [x] 类型约束、初始化状态、变量名和源码位置与数值负载分离。
- [x] 为可能承载 NoValue 的变量保留旁侧状态或动态表示；不能仅凭声明类型假定槽始终为数值。
- [x] 当前 Debug `sizeof(BytecodeValue)=72`、`sizeof(Object)=248`；槽尺寸增加不能直接换算成整体内存退化，四池容量和空闲链已同时删除。

### 2. 统一参数与调用帧

- [x] 脚本调用的局部 `RegisterSlots` 仅为同一底层寄存器文件的 `base/count` 窗口，不创建独立值数组。
- [x] 调用帧记录窗口基址、使用范围、返回位置及必要执行状态，指令使用帧内相对槽号。
- [x] 调用准备阶段预留连续形参/局部槽，`BindArgument` 将实参结果直接移动到形参槽。
- [x] 在实参求值前预留目标窗口，嵌套调用不能覆盖已求出的实参；保持求值和转换报错顺序。
- [x] 局部变量和临时值使用统一窗口分配机制，返回时释放资源并恢复位置，保留容量。
- [x] 正确处理递归、宿主重入、窗口扩容和早退；跨扩容不保存失效的裸元素引用。
- [x] 临时结果最后一次使用可移动；调用者仍使用的变量和容器通过 COW 保持原有值传递语义。

### 3. 删除内部 Object 兼容路径

- [x] 删除 RegisterSlots::objects/reference 双轨存储，消除物化后切换值源的机制。
- [x] 参数绑定、声明、赋值、转换、条件、运算及返回全部使用槽与独立类型描述。
- [x] 常量转为内部值表示；宿主边界执行必要的 Object 导入导出。
- [ ] 自定义运算和类型回调保留公开 ABI，但不得使内部槽长期转成 Object 存储。
- [x] 错误报告通过诊断描述取得名称、源码位置及 NoValue 来源，不给普通值附带重复字符串。

### 4. 全局变量槽编号绑定

- [x] 编译或模块装载时解析全局名称，当前先建立稳定名称链接；字节码日常值读写仍待迁入权威全局槽。
- [ ] 名称表仅服务宿主注册、动态名称接入和模块链接，不用于已绑定变量的普通读写。
- [ ] 保持跨脚本变量、持久函数、宿主修改和重入的一致性，避免两份独立可变值。
- [ ] 明确新变量接入、槽编号稳定性和链接失效处理，不以不安全的名称缓存掩盖哈希访问。

#### STL variant 恢复与执行期全局值结论（2026-09-13）

将手写 16 字节 `CompactValue` 恢复为继承 `Object::Storage` 的薄封装，即
`variant<monostate, int64_t, double, bool, any>`；复制和移动显式操作 variant 基类，避免泛型构造／赋值
递归选择。Debug x64 完整回归为 77/77，当前 `sizeof(BytecodeValue)=72`。冻结 16 字节实现后按
A/B/B/A 比较，首轮两批中位数平均为 94.039ms 对 93.896ms，variant 约快 0.15%；撤回后最终复测
为 94.773ms 对 94.746ms，差约 0.03%，四批均为 14 次 PI 输出一致、长度 502。手写值结构没有
可测优势，后续保留 STL variant，不再以缩小单槽为独立优化目标。

随后验证了纯 `std::any` VM 值候选：外层 any 直接保存 `monostate/int64_t/double/bool`，资源值保存
内层 `std::any`，公开 `Object::Storage` ABI、`VmArray` 唯一数组表示和边界转换均不变。该候选 Debug x64
完整回归 77/77，`sizeof(BytecodeValue)` 从 72 降至 64。首次 A/B 的 variant 约 100ms、any 约 120ms，
后来确认两者都包含新增 `RegisterSlots::export_object()` 后引入的性能回归：`export_argument()` 先复制
导出整份值，再用 `take_storage()` 覆盖，导致资源和数组边界重复转换。修复为移动导出直接组装元数据后，
当前 variant 单批中位数恢复为 93.526ms（92.295--93.897ms），与此前 90 多毫秒档一致。

基于修复后的相同热路径重建纯 any，Debug 仍为 77/77。A/B/B/A/A/B 六批交错中，variant 三批中位数
平均为 93.604ms，纯 any 为 122.185ms，退化约 30.53%；六批均为 14/14 输出一致、长度 502。
说明缩小 8 字节带来的缓存收益远小于数值热路径上的 `any_cast/type_info` 成本。纯 any 候选已撤回，
最终 variant Debug 再次 77/77。

曾尝试在 `Machine` 中给宿主 `global_variables` 叠加 `global_values/global_slots` 缓存；该方案在 typed
array、宿主替换全局、嵌套执行和重复 Session 上形成两份可变真相，并且 `VmArray` 转换无法独立保留
`ObjectVector` 的元素类型元数据，因此已完整撤回。后续执行期全局值必须是唯一权威表：普通读取、赋值、
索引、容器方法和复合写入全部落在同一 VM 槽；宿主回调、公开结果及嵌套 Session 是明确的权威转移边界，
在进入边界前统一导出、返回后统一重新导入。不能再以逐名称失效或读缓存方式逐步迁移。

分阶段探针还验证了一个更严格的约束：若只在 `Session::run` 和宿主回调边界增加整表导入／导出，
但执行路径仍写宿主 Object，退出导出会用入口槽快照覆盖本次执行的新值，双后端回归会立即失败。
该半迁移已撤回并恢复 Debug 77/77。已保留经回归验证的非破坏 `RegisterSlots::export_object()`，
供完整迁移时使用；下一实现批次必须同时切换全局普通读写、别名、成员、索引、容器方法与宿主边界。

### 5. 容器内部值槽化

- [x] 数组元素脱离 Object，使用内部 `CompactValue`；map 的值和结构体字段仍待迁移。
- [ ] 元素类型约束尽量存容器描述，不在每个元素重复保存。
- [x] 数组索引、容器方法接收者、参数及结果直接使用内部值和槽；map/结构体成员仍待迁移。
- [x] 数组保留按值隔离、扩容行为、未初始化检查、范围快照及资源释放时机。
- [x] VM 数组唯一表示为 `VmArray`，宿主 `ObjectVector` 只在公开 Object 边界转换；内部共享句柄写时分离，不改变可观察值语义。
- [ ] map 的键查找可保留哈希结构；容器键查找与变量名解析是不同问题。

#### 数组深复制与边界转换 Profile（2026-09-13）

`CIFA_VALUE_PROFILE` 对原样 `cifa/calc-pi.c` 的 7 次优化 VM 执行累计统计为：数组导入
52,507 次、2,967,965 个元素；数组导出 26,236 次、847,924 个元素；CompactValue 数组深复制
16,394 次，但仅复制 1,806 个元素。导入中 52,465 次来自 const Object，move 导入为 0；导出
全部 26,236 次来自 `take_storage()`，copy 导出为 0。PI 的语言深复制不是主要流量，热点是脚本
隐式全局数组在宿主 `ObjectVector` 与 VM `VmArray` 间反复转换。

检查 const 导入实现后发现，原 `import_resource(std::any value)` 会先复制整个 `std::any`，从而深复制
一份 `ObjectVector`，随后再逐元素构造 `VmArray`。现已拆为 `const std::any&` 与 `std::any&&` 重载：
const 数组直接遍历原 ObjectVector，非数组资源仍保持值复制语义，移动导入仍逐元素移动。该修改不减少
上述边界次数和元素数，只删除每次 const 数组导入前的一份完整 ObjectVector 预复制。

Debug x64 完整回归为 77/77。冻结修改前未插桩 Release 可执行文件后，按 A/B/B/A 运行现有
`bytecode_benchmark`；四批均为 14 次 PI 输出一致、长度 502。基线两批优化 VM 中位数为
95.877/95.332ms，候选为 91.782/91.934ms；两批中位数平均从 95.604ms 降至 91.858ms，
约下降 3.92%。下一步应让执行期全局值直接驻留于
CompactValue 槽，并只在真正宿主可观察边界同步，以消除约 296 万入站和 85 万出站元素转换；必须同时
保持跨脚本持久全局、宿主修改、注册回调观察和嵌套执行语义。

#### 执行期权威全局槽迁移（2026-09-13）

`Machine` 新增持久 `global_values`、name-to-slot 表和存在性表。执行期普通全局读写、alias、成员、
索引、数组声明与容器方法均切换到该 `RegisterSlots`；模块全局链接缓存保存稳定 slot 编号，不再缓存
可能因宿主 `unordered_map` rehash 失效的 `Object*`。`NamedValueRef` 只表达槽后端；map/struct 字段
暂时仍是权威槽 payload 内部的 `Object`，但不再指向宿主全局镜像。

最外层及嵌套 `Session::run()` 在入口导入宿主全局、退出导出；注册宿主函数和自定义二元运算符回调
前导出、返回后重新导入，因此回调可观察脚本最新全局，回调替换/新增全局后脚本也立即可见。共享
Machine、重复 Session、宿主扩容导致 rehash、嵌套执行和 typed array 元数据均由现有回归覆盖。
注册类型转换和 `host.equal()` 不能直接套用同样的整表重导入：这些调用可能发生在持有槽内引用期间，
重导入会使当前引用失效；试验曾在 registered type 测试处异常中止，已撤回。若后续要求这些回调也能
修改全局，应改成版本/dirty 驱动的延迟刷新，而不是在任意用户回调后立即整表覆盖。

Debug x64 最终完整回归为 77/77。`CIFA_VALUE_PROFILE` 使用现有 14 轮 benchmark（其中 7 次优化 VM）
累计结果保持 14 次输出一致、length=502：数组导入 36,211 次、1,778,917 个元素；数组导出 36,246 次、
1,786,134 个元素；copy 3,297,959 次，`copies_unattributed=0`。与迁移前导入 2,967,965 个元素相比
下降约 40.1%。当前剩余大数组转换集中在 `println` 等真实宿主可观察边界的整表同步，不再发生于
普通脚本全局读写；若继续优化，应增加 per-global dirty/version 同步，避免无关全局随每次宿主调用往返。

未插桩 Release 两批 optimized VM 7 次样本中位数分别约 88.39ms 和 89.43ms，两批均
`PASS: all 14 outputs identical, characters=502`。相对上一批候选中位数均值 91.86ms，跨批次约再下降
3.2%；这不是冻结旧二进制后的严格交错 A/B。

#### 剩余复杂资源复制审计（2026-09-13）

修正 `CIFA_VALUE_PROFILE` 口径：`CompactValue` 复制赋值过去没有计入 `resource_copies` 和数组深复制，
因此此前 `array_copies` 只覆盖复制构造，严重低估复杂值复制。现在复制构造和复制赋值统一统计，并增加
字符串复制字节数、map 条目数以及按 opcode 的资源/数组复制归因；这些修改仅存在于 Profile 编译。

修正口径后的 PI 7 次优化 VM 显示：`RegisterSlots::copy()` 3,297,959 次中，3,213,763 次来自
`LoadLocal`，但这些几乎全是数值；`LoadLocal` 的数组深复制只有 14 次、1,806 个元素。真正复杂复制热点是
动态 `Load` 的 26,229 次/2,120,041 元素和 `Store` 的 52,472 次/1,695,848 元素。总体
`VmArray` 深复制 95,095 次、3,817,695 元素，另有 1,015 次字符串复制、约 240KB。

增加了一个独立的复杂值复制探针。128 个 62 字节字符串组成的数组反复跨函数按值传递 200 次时，产生
1,204 次数组深复制、153,984 个元素和约 9.56MB 字符串复制；
其中 `LoadLocal` 本身占 400 次、51,200 个元素。再加入 64 项字符串 map、100 次按值传递后，额外观察到
603 次 map 深复制、38,592 个顶层条目；map 内字符串的递归复制尚未完整归因，因此该数字是保守值。
这证明减少复制对非简单类型任务有直接意义，不能只依据 PI 的数值元素成本判断。

普通全局整槽 `Store` 已改为从 RHS `RegisterSlots` 直接赋到权威全局槽，删除内部
`VmArray -> ObjectVector -> 临时槽 -> VmArray` 往返，不改变语言要求的按值深复制。Debug 77/77。
PI 数组导入元素从 1,778,917 降至 930,993，导出从 1,786,134 降至 938,210；复杂数组探针导入
从 25,728 降为 0，导出从 25,984 降为仅最终公开结果的 256。语言值复制量保持 153,984 个数组元素和
约 9.56MB 字符串，说明边界转换与值语义复制已被清楚分离。

未插桩 Release 两批 optimized VM 中位数约 88.66ms 和 85.47ms，均 14/14 输出一致。下一优先级应是为
`VmArray`/`ObjectMap` 设计保持深复制可观察语义的 copy-on-write，或在编译期证明只读的参数/临时值上
使用借用；不要继续针对 321 万次数值 `LoadLocal` 增加运行时 tag 分支。

#### 返回寄存器直写与复杂值 COW（2026-09-14）

脚本函数调用继续使用同一 `Machine::registers` 底层文件：调用准备阶段通过 `enter()` 预留连续形参和
局部槽，`local_windows` 只保存共享存储的 `base/count` 视图。返回路径已删除固定 `call_result` 中转槽；
`Return` 现在按调用帧保存的绝对目标位置直接把结果移动到调用者结果寄存器，空返回也直接在该槽生成
`NoValue`。返回类型转换在目标槽完成，随后恢复调用者窗口，不再执行第二次结果移动。

`VmArray::values` 和新的内部 `VmMap::values` 使用 `shared_ptr` 持有底层容器。`CompactValue` 复制数组或
map 时仅共享存储；索引写、扩容、`push_back/pop_back/insert/erase/clear`、map 字段写和结构体成员写在
首次修改前 detach。公开边界仍保持 `ObjectVector/ObjectMap`，内部 `VmMap` 的自动类型身份映射为
`ObjectMap`，因此注册 API 和结构体类型描述不暴露内部包装类型。

值传递回归同时覆盖数组和 map：形参内修改不得污染原值，返回结果再次修改也不得污染原值；Debug x64
完整回归为 77/77。复杂探针保持结果 `128126`：`resource_allocations` 从 172,948 降至 18,964，
`string_copies` 从 154,241 降至 257，字符串复制量从 9,559,166 字节降至 12,158 字节；纯传递场景的
数组深复制从 1,204 次/153,984 元素降为 0，map 从 603 次/38,592 项降为 0。数组宿主导入/导出量不变，
说明 COW 删除的是语言内部按值传递复制，不是掩盖边界转换。

未插桩 Release 两批 optimized VM 中位数为 87.39ms 和 85.54ms，均 14/14 输出一致；改动前约为
88.66ms 和 85.47ms，当前可判定无回退。

#### COW 后剩余复制审计（2026-09-14）

COW 初版后的 PI Profile 仍报告 29,498 次数组 detach，其中 `Index` 占 13,118 次、复制
1,060,024 个元素。根因不是语言写时分离，而是优化执行器的一维数组读取快路通过非 const
`values[index]` 取得元素，导致每次读取共享数组都错误触发整数组 detach。纯读路径现改为 const 访问；
仅越界读取需要按既有语义扩容时进入可写路径。Debug 完整回归 77/77，修复后 PI 的非空数组深复制从
1,060,024 个元素降为 0。剩余 16,380 次 `MethodPush` detach 的容器长度均为 0，只分配即将写入的空
vector，不复制元素；这是共享空值首次修改所需的所有权分离，不属于大量数据复制。

修复误 detach 后，PI 最大剩余数据搬运是宿主边界：数组导入 930,993 个元素、导出 938,210 个元素。
VM Profile 将其归因为 129 次宿主调用，其中 `to_string` 127 次、`println` 2 次；`call_host()` 为允许任意
宿主回调读取或修改全局变量，每次都必须整表导出并在返回后重导入。受内建 generation 校验保护的
`call_builtin_math()` 现增加基础类型 `to_string` 快路；只有当前函数仍是构造期内建版本，且参数为空、
整数、double 或字符串时才跳过宿主边界，用户覆盖或其他对象类型仍回退原调用路径。

上述快路后 PI 数组导入降为 9,975 次/18,662 个元素，导出为 383 次/17,423 个元素；总边界元素从
1,869,203 降至 36,085，减少约 98.1%。剩余主要来自两次用户注册的 `println`，该回调对 VM 而言可
任意观察或修改全局，不能在不扩展宿主 API 纯度契约的情况下跳过同步。PI 中 `resource_copies=96,110`
仍包含 COW 句柄的浅复制；实际字符串复制仅 1,015 次/240,135 字节，数组和 map 的非空深复制均为 0。

复杂数组/map 探针现在 `array_imports=0`、`array_exports=2/256 elements`，即仅 Session 最终公开全局结果；
数组和 map 深复制仍均为 0，字符串复制 257 次/12,158 字节。两批未插桩 Release optimized VM 中位数为
77.39ms 和 78.83ms，均 14/14 输出一致；相对上一轮 87.39/85.54ms 的两批平均约下降 9.8%。

#### 独立 Native ABI 与寄存器内建（2026-09-14）

`CifaBytecode` 新增独立 `register_native_function(name, NativeCallContext&)`。回调通过受控上下文读取
`int64_t/double/bool/string` 或 typed resource，并将结果直接写入调用结果槽；普通 `Opcode::Call` 不再构造
`ObjectVector`，也不执行 `Cifa::register_function(ObjectVector&)`。传统宿主函数 API 仅保留给 Cifa/AST。
native 名称通过独立的编译期可见集合参与静态检查，不再向旧 `functions` 表注册 dummy 占位；优化器也直接
查询实际 `native_functions[name].builtin`，因此覆盖 `sin/floor/size` 后会正确禁用常量折叠和专用指令。

VM 内建现覆盖输出、字符串/数值转换、类型、尺寸、`sprintf/format`、`ifv/ifvalue`、`max/min` 及常用一元、
二元数学函数。`sprintf/format` 直接读取寄存器 payload，支持现有 `%lld/%llu/%x/%f/%s/%%` 与 `{}`、索引、
精度格式测试；纯整数 `min/max` 保持 `int64_t` 精确比较。`type()` 同时识别 NumericBinding、NoValue 和
注册数值别名的规范基础类型。

嵌套 `run_string/run_file` 在执行中先发布当前 VM 全局供内层编译，使用独立执行寄存器文件，并将内层
runtime error 传播给父 VM；普通顶层连续执行仍以宿主新注册参数为权威。重入编译的静态错误集合会追加，
普通连续 `run_script` 则继续清除上一轮错误。优化模块在 native 或脚本函数表于调用中变化后立即中止。

数学专用指令 `MathUnary/MathBinary` 的非数值参数错误也已改为直接基于寄存器生成转换诊断，不再物化
`ObjectVector` 或回落 `Machine::call_host`。当前 `call_host` 只保留未引用的兼容实现；普通 native、内建数学
和优化数学指令均在寄存器路径内完成参数读取、计算与结果写入。

测试入口已明确分为共同、Cifa 专用和 CifaBytecode 专用三组。所有依赖函数注册的测试均从共同组移出，
分别以传统 `register_function` 和 Bytecode `register_native_function` 独立运行、独立计数，完整 Debug 回归为
88/88。重新编译的 `CIFA_VALUE_PROFILE` 运行 14 个 PI 后端样本全部输出一致，
其中 7 次优化 VM 累计 `object_imports=896`、`object_exports=7`；7 次导出正好对应最终公开结果，普通 native
调用未产生 Object 参数/结果边界。数组和 map 非空深复制仍为 0，`object_copies=0`。

复杂数组/map Profile 结果保持 `128126`：`copies=3852`、`object_copies=0`、`array_copies=0`、
`map_copies=0`，字符串复制 257 次、12,158 字节；两次数组导出共 256 个元素，只发生在 Session 最终公开
结果边界。现有证据中没有由非空数组、map 或 Object 深复制形成的大量复制热点，主要计数仍是标量寄存器
移动和 COW 资源句柄浅复制。

#### Lua 5.4 字节码长度与执行效率对比（2026-09-14）

新增只读 `CifaBytecode::bytecode_statistics()`。500 位 PI 的七个共同算法函数
Cifa 共 464 条静态指令，Lua 5.4.5 共 379 条，Cifa 多 22%；Cifa 含顶层计算共 579 条，Lua 七函数加
`calculate_pi` 共 442 条。单次动态执行 Cifa 为 4,473,343 条，Lua 由 instruction hook 测得 3,952,869 条，
Cifa 只多 13.2%。同机无插桩 Release 中位数为 78.3302ms 对 9.3437ms，Cifa 慢 8.38 倍；按动态条数折算，
单条指令成本约慢 7.41 倍。因此当前主因不是字节码数量，而是每条通用指令的分派、富元数据、显式操作数池、
类型/来源旁表和动态容器检查成本。

当前 `Instruction=104` 字节，PI 579 条结构体有效长度约 60KB；Lua 32 位指令的 442 条核心码约 1.7KB。
但既有 40/44 字节独立执行流 A/B 已证实会退化 2%--3%，不能再次只做机械压缩。后续优先融合热序列：
局部数值 load/binary/store、循环 scope/mark/branch/jump/increment，以及局部 typed VmArray 的 index/push；
只有专用 opcode 确实减少必读字段后，才原地缩小单一指令流。

#### switch 分派与单指令固定成本（2026-09-14）

Lua 5.4 的 `luaV_execute` 在无 hook 的 Release 常态路径中，通过 `vmfetch()` 取一条 32 位指令，再由
`switch(GET_OPCODE(i))`（GCC 可进一步使用 jump table）进入唯一 handler。按展开后的源码结构估算，每条
指令在 handler 外通常只有约 2--3 个有效语句：低概率 trap 检查、取指并递增 pc、opcode 分派；Release
断言不产生运行时代码。简单 handler 也很短，例如 `MOVE` 约 2 个有效语句，`LOADI` 约 3 个，`JMP` 约 1 个。

Cifa 修改前每条指令在 handler 外固定执行取指、构造 `input_slot` 访问器、读取 destination、清空赋值来源、
解析当前源码位置等约 6--8 个源码级操作；随后通过约 40 个顶层平行 `if (opcode == ...)` handler 组线性分派。
按 handler 位置不同，一条指令还会额外经过 0 到 60 多个 opcode 比较。简单 `Jump/Constant/LoadLocal` 因此
并不只承担自己的语义成本，还承担前方大量不相关 handler 的比较和代码布局压力。这是动态指令数只比 Lua
多 13.2%，但单条成本约高 7.4 倍的重要原因之一。

第一版实验只在顶层 `switch` 中 `goto` 到旧 handler，并保留 handler 内 opcode 守卫；该结构虽消除了跨
handler 线性扫描，但不是最终实现，其四批内部 `execute_ms` 中位数平均 64.4500ms、相对旧实现
78.2926ms 下降约 17.7% 的结果只代表过渡版本。该 goto/label 结构已完整删除，不能作为当前代码结论。

当前执行循环是覆盖全部 opcode 的单一真实 `switch/case`：每个 case 直接执行对应语义，不再通过 goto、
label 或 case 内重复 opcode 判断转发。机械迁移曾使优化 `size(value)` 因错误 `break` 跳过寄存器路径，修复为
同一 `Size` case 内分别处理命名值和寄存器值后，Debug x64 完整回归为 89/89。使用
benchmark 程序内部的 `execute_ms` 显示，当前完整 switch 连续四批中位数为
62.6468/63.6289/63.7821/62.3279ms，平均 63.0964ms；相对历史同口径 78.2926ms 跨版本下降约 19.4%。
旧线性 if 源码未在本轮交错重建，因此 19.4% 只作同口径跨版本对照，不标记为严格 A/B。

曾错误使用进程外 `Stopwatch` 包围整个 exe，得到约 87ms 基线并把进程启动、CRT 初始化、脚本编译和 VM
执行混在一起；该口径与历史约 78ms 的内部 `execute_ms` 不可比较，由此得到的 switch 17.1% 和数组
`reserve(8)` 结论均作废。数组预留改动已撤回；过渡 goto switch 的 17.7% 与当前真实 switch 的
63.0964ms 数据必须分别解释。
下一步应继续减少 switch 前仍固定执行的 destination/source/input 准备：让不需要这些字段的 `Jump`、
`ScopeEnter/Leave`、`LoopMark` 等在取指后立即进入短 handler，并逐步把复杂 handler 拆成 Lua 风格的快路与慢路。

新增统计 API 回归后 Debug x64 完整测试为 89/89。

#### 完整 switch 后的计与下一阶段设计（2026-09-14）

对 `execute_instructions` 再次搜索后确认，运行时主循环已不存在大量平行 opcode `if`。剩余直接比较
`instruction.opcode` 的位置只有两组共享 case：`Store/Increment` 用于区分是否生成单位操作数，
`Load/Peek` 用于区分未初始化检查；它们是进入 case 后的语义分类，不是跨 handler 线性分派。其余约百处
opcode 判断位于编译、验证、寄存器规划和诊断帧构建阶段，不按每条运行时指令执行。

当前 PI Profile 的主要 handler 成本为：`MethodPush` 181,860 次/29.17ms，`StoreLocal` 437,558 次/21.90ms，
`RegisterBinary` 584,768 次/18.10ms，`LoadLocal` 459,109 次/10.53ms；插桩总执行 184.78ms。
`MathBinary` 中 `Atan2/Pow/...` 的内部 switch 虽源码较长，但不是 PI 热点，不能按代码长度优先重写。

尝试让 `MethodPush` 对编译期已知的无别名局部数组直接取槽并 append，Debug 89/89，Profile 的
`method_push_ms` 从 29.17ms 降到 27.96ms；但未插桩 A/B/B/A/A/B 中，冻结基线三批中位数平均
62.7904ms，候选 63.0626ms，退化约 0.43%，快路已撤回。随后单独保留无约束 append 两个相同分支的
源码合并，严格 A/B/B/A 中基线两批平均 62.9011ms、合并版 63.7915ms，退化约 1.42%；该合并也已撤回。
这说明源码重复不能直接等价为运行时重复，原分支布局在当前 MSVC `/O2` 下生成的热代码反而更快。

还尝试把每条指令的 `SourceLocation*` 解析改为保存 `SourceRef`、在可能报错的 handler 内通过 lambda 惰性解析。
Debug 89/89，但同口径六批交错中，冻结基线三批平均 64.1203ms，候选 66.2247ms，退化约 3.28%；
lambda/间接访问增加了热运算路径成本，已完整撤回。不能仅因 Lua 惰性取行号就机械复制其表面结构。

下一阶段按 Lua 的核心原则重新设计，而不是继续给巨型 case 增加运行时判断：

- 保留当前通用 `Instruction` 与动态 handler 作为完整语义慢路，承担别名、类型转换、容器、诊断和宿主回调。
- 为 Profile 已证明稳定的局部数值热子集建立单一紧凑微指令格式，固定编码 opcode、目标和两个操作数；
	首批只覆盖当前 PI 中 584,768 次且 fallback=0 的 `RegisterBinary` 数值路径。
- 微指令直接读写已绑定的局部数值槽，遇到编译期无法证明的别名、类型或诊断条件时仍生成现有通用指令，
	不在执行期为每条微指令重复探测全部动态条件。
- 不再建立一份覆盖所有 opcode 的第二执行流；此前 40/44 字节全量 lower 流已退化 2%--3%。微指令必须只替代
	可证明的热序列，并保持通用指令和诊断信息为唯一慢路表示。
- 第一实现批次先统计可替代的动态条数和静态站点，再冻结当前约 63ms 可执行基线；只有 Debug 89/89 且
	未插桩交错 A/B 有稳定收益才扩展到 `StoreLocal/IncrementLocal`。

#### NumericBinary 热数值微指令结果（2026-09-14）

已新增 `NumericBinary`，但没有建立第二套全量执行流。通用 `Instruction` 仍保存冷站点索引、源码位置和
完整 `RegisterBinary` 慢路；验证阶段只把可证明的非写回数值表达式映射到连续
`vector<RegisterOperation>` 热表。每个热操作为 16 字节，编码算术 opcode、左右操作数、临时目标和来源 flags；
执行 case 直接按 flags 从局部、常量或临时槽读取数值，调用 `binary_numbers`，任何守卫失败均落回原
`RegisterBinary`。这保持了单一 `switch/case`，也把复杂类型、别名和诊断语义留在唯一慢路中。

第一阶段只编码局部来源时，PI 动态覆盖 333,468 条，Profile 为 `numeric_binary_fallback=0`；相对真实
switch 基线的未插桩 Release 交错测试约快 3.76%。把热字段从冷站点旁表复制到连续 16 字节操作表后，
同口径再快约 0.74%。继续在 flags 中加入常量和临时来源后，覆盖增至 522,652 条，仍为零回退；相对上一阶段
`numeric_micro_final.exe` 的 A/B/B/A/A/B 三批中位数平均从 59.9636ms 降至 56.9855ms，增量下降约 4.97%。

最终版本相对冻结的真实 switch 基线做 A/B/B/A/A/B：基线三批中位数为
64.2692/62.0130/62.6750ms，平均 62.9857ms；最终候选为 56.5683/56.9179/57.6509ms，平均
57.0457ms，同批累计下降约 9.43%。每批均使用程序内部 `execute_ms`，不包含进程启动和脚本编译。
Debug x64 完整回归为 89/89。最终 Profile 为 `numeric_binary_fast=522652`、
`numeric_binary_fallback=0`、`register_binary_fast=62116`、`register_binary_fallback=0`。

尝试把剩余 62,116 条 typed int/double 声明初始化写回也塞入同一 `NumericBinary` case：Profile 可达到
584,768 条全覆盖、零回退，插桩执行从 164.216ms 降至 161.820ms；但未插桩 Release 增量 A/B 中，原版本
三批平均 57.6466ms，写回版 60.0448ms，退化约 4.16%，故已撤回。原因是写回判断、冷 `VariableSite`
读取和初始化分支被加入全部 584,768 次热 case，其固定成本大于避开 62,116 次通用 handler 的收益。
后续不要在现有 `NumericBinary` 热 case 内重新加入写回分支；如继续优化写回，应使用独立 opcode/case，
使非写回热路径不承担该判断。

#### push 与普通 for 尾部融合实验（2026-09-14）

对 `MethodPush` 增加 receiver 分类 Profile 后确认，PI 中 181,860 次调用全部访问已链接全局槽，局部槽和
动态作用域 receiver 均为 0；原通用路径的 receiver 查找约占 8.08ms。第一版
`NumericArrayPushLocal` 因 PI 动态覆盖为 0，未进入 Release 测试并已撤回。第二版
`NumericArrayPushGlobal` 直接读取已链接全局槽，并只对无元素约束、无参数元数据的 int/double/bool 追加走
短路径；181,860 个站点中约 30,082 次命中，151,778 次仍回落 `MethodPush`。虽然插桩总执行从约
164.10ms 降到 159.86ms，未插桩 Release A/B/B/A/A/B 中基线三批平均 57.4393ms，候选
61.3723ms，退化约 6.85%，故 opcode 和临时计数均已撤回。结论是当前脚本的大多数 push 参数仍带槽元数据，
独立 receiver 快路不足以覆盖完整数据流，双路径守卫成本反而更高；后续若继续优化 push，应先改变数组参数的
表示或生成真正端到端的 push 操作数编码，不再只绕过 receiver 查找。

普通 `for` 的更新尾部新增 `NumericForNext`。编译器只在已知 ordinary-for 更新表达式中，将丢弃结果的
`IncrementLocal` 显式标记为专用 opcode，并编码条件入口 PC；运行时对无别名、无显式类型约束的整数槽直接
递增并回跳，守卫失败则 fallthrough 到原 `IncrementLocal`，随后继续原 `Leave + Jump` 序列。不能在 verify
中仅按 `IncrementLocal + Leave + Jump` 邻接形态猜测：该实验曾误识别其他控制流并触发
`bytecode argument window was not prepared`。此外新回跳目标保存在 `auxiliary`，必须在 `compact()` 删除
Enter/Leave 时随 PC remap；遗漏 remap 会跳入错误指令并破坏脚本调用参数窗口。

最终 Profile 中 `NumericForNext` 执行 182,322 次，普通 `IncrementLocal` 从 182,847 次降至 525 次，
同时跳过约 182,322 次 `Jump`；插桩总执行约为 161.56ms。未插桩增量 A/B/B/A/A/B 中，NumericBinary
基线三批平均 57.2823ms，加入循环融合后为 56.5530ms，下降约 1.27%。相对冻结真实 switch 基线的最终
累计 A/B 中，基线三批 62.2394/62.7501/63.1567ms，平均 62.7154ms；最终版本
57.1135/57.1776/56.6503ms，平均 56.9805ms，同批累计下降约 9.14%。Debug x64 完整回归为 89/89，
并新增普通 for 必须生成单条 `NumericForNext` 且 `sum_to(10)==45` 的结构测试。

#### NumericCompareBranch 与空作用域实验（2026-09-14）

`compact()` 现将可证明的 `NumericBinary + Branch` 融合为独立 `NumericCompareBranch`。热操作仍使用
16 字节 `RegisterOperation`，执行时直接读取数值操作数并按比较结果设置 PC；守卫失败才顺序落入原
`RegisterBinary` 与 `Branch` 慢路。最终 Profile 覆盖 276,172 次，普通 `Branch` 从 309,709 次降至
33,537 次；插桩执行从约 161.56ms 降至 145.54ms。未插桩 Release A/B/B/A/A/B 中，冻结
`NumericForNext` 基线三批平均 57.4406ms，候选三批平均 53.1436ms，增量下降约 7.48%。Debug x64
完整回归为 89/89，并增加比较表达式必须生成融合指令的结构测试。该版本是当前性能基线。

随后对 `ScopeEnter/ScopeLeave` 增加 Profile 分类。PI 中 215,397 次进入里，123,611 次具有静态 binding，
91,786 次无 binding，range binding 为 0；退出累计清理 302,401 个局部槽。实验新增独立
`EmptyScopeEnter/EmptyScopeLeave`，只对函数内 `operand==0 && auxiliary!=0` 的静态空作用域跳过真实
`Scope` 容器 push/reserve/pop，同时保留 `local_scope_bases` 和 Unwind 清槽语义。Profile 实际命中
91,658 对，真实 scope 操作下降约 42.6%，插桩执行从 145.55ms 降至 135.27ms；但同一
`bytecode_benchmark.cpp` 的未插桩交错 A/B 中，基线三组均值约 52.91ms，候选约 54.19ms，退化约
2.4%。说明新增 opcode 对热 switch 布局和前端的扰动大于容器操作收益。该专用化已撤回，Profile 分类保留；
后续不要再以独立空 scope opcode 重试，循环/块优化必须能同时删除更多控制流指令才值得评估。

#### ArrayPushGlobal 端到端全局数组追加（2026-09-14）

旧 `NumericArrayPushGlobal` 只在参数槽无绑定、无类型和无来源元数据时命中约 30,082 次，其余
151,778 次回退通用 `MethodPush`，Release 退化 6.85%，已撤回。本轮改为独立 `ArrayPushGlobal`：编译器
显式记录 receiver 是否可证明为全局名称，运行时复用已有 `linked_global(name_id)` 缓存直接取得权威全局槽，
无约束数组将完整参数 payload 移入 `VmArray`，不再检查参数槽元数据；typed array 仍在同一冷分支执行原有
导出和类型转换。函数内已分配标量槽或已声明数组的同名 receiver 保留通用 `MethodPush`，不能用
`local_slot==0` 反推全局，因为函数局部数组不属于标量局部槽表。

PI 的 181,860 次 push 全部进入 `ArrayPushGlobal`，没有参数 fallback；Profile 的 `method_push_ms` 从约
28.8ms 降至 9.8ms，插桩总执行从 145.5ms 降至 126.6ms。使用相同
benchmark 程序的未插桩 A/B/B/A/A/B 中，`NumericCompareBranch` 基线三组截尾均值约
53.56/53.92/53.10ms，平均 53.53ms；候选约 50.01/49.39/49.51ms，平均 49.64ms，增量下降约
7.3%。Debug x64 完整回归为 89/89；结构测试同时要求全局数组生成专用 opcode、函数局部数组保留
`MethodPush`。该版本成为新的当前性能基线。

#### 当前六项推进路线（2026-09-14）

以下顺序以当前约 49.64ms 的 `ArrayPushGlobal` 版本为基线；每项仍需先 Profile，再做 Debug 89/89 和
同 probe 未插桩交错 A/B，不能根据插桩耗时直接保留。

1. [x] `LoadLocal` 消费融合：统计紧邻且唯一的消费者，让高覆盖消费者直接读取局部槽，删除加载和中间复制。
2. [x] producer + `StoreLocal` 融合：按 RHS 生产者分类，使用独立 opcode 完成计算、类型约束和写槽；不得向
	现有 `NumericBinary` 热 case 增加写回判断。
3. [x] 数组读取直接消费：针对 `Index -> push/数值运算/StoreLocal` 等高频序列，避免数组元素先写临时槽。
4. [x] 减少指令公共前置成本：让无需 destination、输入表或源码解析的短指令避开无关准备；不重试已退化的
	source lambda 惰性解析实现。
5. [x] 普通数值循环整体融合：只有能同时删除多条 scope、mark、compare、branch、update、jump 控制指令时
	才实施；不再新增独立空 scope opcode，也不按邻接形态猜测 ordinary-for。
6. [x] 表达式描述符重构：长期让局部槽、常量、临时值、全局槽和数组元素身份保持到最终消费者，仅在语义要求时
	物化中间寄存器；这是继续逼近 Lua 单条指令成本的架构方向。

第一项 Profile 显示 459,109 次 `LoadLocal` 全部在后两条指令内被唯一消费：367,579 次由紧邻指令消费，
91,530 次由下两条指令消费，后者全部是 `MathBinary` 的第一个参数。紧邻消费者中 `Index` 为 213,468 次、
`ArrayPushGlobal` 为 91,192 次、`MathBinary` 为 61,111 次，其余仅 1,808 次。由此先实现范围最窄的
`LoadLocal + ArrayPushGlobal -> ArrayPushGlobalLocal`：verify 在控制流分析前融合并标记原加载为待 compact
删除，专用指令直接从局部槽复制 payload 到全局数组；别名、未初始化错误、typed array 转换和数组值语义仍
保留。该融合精确删除 91,192 次 `LoadLocal`，动态 LoadLocal 从 459,109 降至 367,917，Profile 的
`local_load_ms` 从约 11.2ms 降至 8.9ms，插桩总执行从约 126.2ms 降至 121.4ms。

使用相同 `bytecode_benchmark.cpp` 的未插桩 A/B/B/A/A/B 中，`ArrayPushGlobal` 基线三组截尾均值为
49.33/49.41/49.10ms，平均约 49.28ms；融合候选为 47.86/48.06/48.06ms，平均约 47.99ms，增量下降约
2.6%。Debug x64 完整回归为 89/89，并增加全局数组追加局部参数必须生成单条融合指令且不保留对应
`LoadLocal` 的结构测试。下一子项按覆盖优先评估 `LoadLocal -> Index`，但需先限制到单维整数数组快路，
避免首版同时承担 map、多维索引和动态声明语义。

补结构测试时发现，编译期 `Removed` 标记最初在 verifier 中与 `Exit` 一样直接终止当前控制流分析，导致
后续指令没有生成 destination/input 映射，并在完整测试中触发 `0xC0000005`；正确语义是零栈效果后继续
merge 到下一条，只在 compact 阶段删除。修正后最终 Debug 为 89/89。为排除修正影响又重建 Release 做
A/B/B/A：两组基线截尾均值 49.27/49.71ms，平均约 49.49ms；最终候选 48.68/48.28ms，平均约
48.48ms，增量下降约 2.0%。因此保留融合，但以后所有编译期删除标记都必须参与完整控制流传播。

随后实现 `LoadLocal + Index -> IndexLocal`，仅覆盖非声明、非字符串、单维索引；专用指令直接从已绑定
局部槽读取整数下标，同时保留别名解析、未初始化诊断、数组越界扩容、map/string 慢路和精确源码位置。
PI 中该指令覆盖 213,468 次，普通 `Index` 从 213,706 次降至 238 次，动态 `LoadLocal` 从 367,917 次
降至 154,449 次；插桩执行约从 121.4ms 降至 110.4ms。相同 benchmark 的未插桩交错 A/B 中，基线
约 48.75ms，候选约 44.59ms，增量下降约 8.5%。Debug x64 完整回归为 89/89，并加入结构测试。
该版本是当前性能基线。

第二项 producer Profile 显示，`StoreLocal` 的主要 RHS 为数学一元结果和数组读取：typed numeric 路径中
`MathUnary` 61,111 次、`IndexLocal` 30,206 次；numeric assign 路径中数学相关 29,953 次、
`IndexLocal` 59,853 次。两种独立写回 opcode 均完成 Debug 89/89 后被 Release A/B 否定：
`MathUnaryStoreLocal` 只覆盖 29,953 次，基线约 44.73ms、候选约 46.64ms，退化约 4.3%；
`IndexLocalStoreLocal` 覆盖 59,853 次并删除等量动态 `StoreLocal`，Profile 从约 126.2ms 降至 106.9ms，
但未插桩基线约 45.12ms、候选约 46.03ms，退化约 2.0%。两项均已撤回。结论是仅把 producer 与
赋值语义塞进更大的 switch case 会破坏前端和热布局，动态指令减少不足以补偿；后续 Store 消除必须由
表达式目标描述符直接生成到既有局部槽，不能继续新增复制整段执行逻辑的组合 opcode。

第四项删除了执行循环对 `register_assignment_source` 的逐指令无条件清空。该标记只在三条
`RegisterBinary` 赋值慢路设置，现在改为赋值成功出口清空；赋值报错期间仍保留诊断帧截断语义。
Debug 89/89，错误正文、源码行和 `^` 对齐保持不变。未插桩交错 A/B 中基线约 44.58ms、候选约
44.40ms，稳定快约 0.4%，因此保留。源码位置惰性 lambda 的历史负实验不再重试。

第五项尝试让 `NumericForNext` 在整数更新后直接执行下一轮 `NumericCompareBranch` 条件并跳到 body 或
循环出口；首次条件、break/continue、别名和非整数路径仍保留原控制流。Debug 89/89，Profile 中
`NumericCompareBranch` 从 276,172 次降至 123,928 次，少分派 152,244 次；但 Release 基线约
44.81ms、候选约 46.85ms，退化约 4.6%。复制比较读取和六路关系判断扩大了 update 热 case，已撤回。
现有 `NumericForNext` 的 update+jump 融合继续保留；整体循环优化若继续推进，必须改为独立紧凑循环描述符
或小型循环执行器，不能把另一个完整 handler 内联到现有 switch case。

第六项为 `CallSite` 增加二元数学局部操作数描述符。仅当 `MathBinary` 的两个参数都能在编译期解析为局部槽
时省略两条 `LoadLocal`；执行时直接读取两个局部 payload，动态别名才物化到 scratch，并保留参数级
未初始化错误和转换位置。PI 中 `LoadLocal` 从 154,449 次降至 32,227 次，删除 122,222 次加载；最终
Release 两轮 A/B 中公共前置基线约 44.54ms，候选约 43.28ms，增量下降约 2.8%，相对早先约 44.6ms 的
`IndexLocal` 基线累计约快 3%。Debug 89/89，并增加 `pow(left, right)` 必须不生成 `LoadLocal` 的结构测试。
逐参数 mask 扩展曾进一步把 `LoadLocal` 降至 1,808 次，但每次数学指令的 mask 分支和输入映射使 Release
从双局部约 42.96ms 退化到约 45.81ms，已撤回。结论是表达式描述符必须产生固定形态的执行路径；高覆盖、
无运行时形态分支的描述符有收益，通用逐操作数 mask 不适合当前巨型 switch。

重新生成当前 43ms 版本的 Profile 后，动态热点已经转移为 `StoreLocal` 437,558 次、
`NumericCompareBranch` 276,172 次、`NumericBinary` 246,480 次、`LoopMark` 218,911 次、
`ScopeEnter` 215,397 次和 `ScopeLeave` 213,522 次；普通 `LoadLocal` 仅余 32,227 次。相邻转移统计显示
`ScopeEnter -> LoopMark` 215,397 次，`NumericCompareBranch -> ScopeEnter` 182,326 次，
`ScopeLeave -> ArrayPushGlobalLocal` 91,192 次。实验将无直接执行入口的前两者融合，并进一步把固定的
`ScopeLeave -> ArrayPushGlobalLocal` 清槽/释放逻辑并入数组追加，合计减少约 398,000 次动态分派。
Debug 完整回归仍为 89/89，但未插桩 A/B/B/A/A/B 中关闭融合的基线三批中位数均值约 43.99ms，候选约
44.84ms，退化约 1.93%，已全部撤回。结论与空 scope、NumericForNext 内联比较实验一致：当前巨型
switch 中，即使删除数十万条动态指令，只要扩大既有热 handler 或增加模式字段判断，前端和代码布局代价
仍可能超过收益。

后续不再尝试相邻 opcode 融合。若目标是数个百分点以上的明确改善，优先方向应是：一，将当前 104B
诊断/通用指令和固定形态热执行码分离，在编译完成后生成连续紧凑的热码流，同时保留原指令索引用于错误位置，
避免此前“第二执行流”实验中逐条旁表回查；二，让表达式目标描述符直接选择局部数值槽，使
`NumericBinary/IndexLocal/MathUnary` 直接写最终槽并由独立固定格式 handler 执行，而不是复制现有
`StoreLocal` 语义到组合 opcode。`StoreLocal` 当前 437,558 次中 248,475 次为 typed numeric 初始化，
180,998 次为 untyped numeric assign，这两类合计占 98.1%，是下一轮设计的主要目标。

#### 直接目标槽试验（2026-09-14）

实现 `NumericBinaryLocal`：原本在赋值生成阶段已有目标局部槽的整数二元运算，现在不再降级为
`RegisterBinary` 后写临时寄存器，而是读取固定数值操作数后直接写最终 `active_locals` 槽。typed `int`
初始化会建立绑定；无类型局部赋值会保留既有 numeric binding；别名、double、比较结果、非数值和异常路径
仍回退原 `RegisterBinary`，以保持错误帧和转换语义。新增结构测试要求 `int value = left + right; value =
value * 2;` 只生成两条 `NumericBinaryLocal`，不再生成 `StoreLocal`；Debug x64 为 89/89。

当前 PI Profile 命中 62,116 次 `NumericBinaryLocal`，`RegisterBinary` 热计数为 0。未插桩
A/B/B/A/A/B 的基线三批中位数均值约 44.26ms，候选约 44.42ms，差异约 -0.36%，没有明确收益。保留该
直接目标槽语义作为固定格式执行的基础，但不把它视为单独性能胜利；下一步应优先把同一模式推广到高覆盖的
`IndexLocal` 和 `MathUnary` 局部目标，或改由紧凑热码流承载，而不是向 `StoreLocal` 增加更多判定。

随后实现更窄的 `ConstantLocal`：只折叠相邻的 `Constant -> StoreLocal`，且仅限函数内 typed `int`
初始化和整数常量。该 opcode 直接写最终局部槽、建立 `Int` binding、绑定当前 scope，并保留未丢弃赋值
表达式的结果复制；普通 `Constant`、`StoreLocal` 和其它类型的 handler 不增加分支。Profile 命中 91,349
次，`StoreLocal` 从 437,558 降至 346,209，插桩执行约从 104.8ms 降至 101.0ms。Debug x64 为 89/89，
结构测试要求 `int value = 7;` 生成一条 `ConstantLocal` 而不保留 `StoreLocal`。未插桩交错
A/B/B/A/A/B 中，baseline 三批中位数均值约 43.51ms，candidate 约 42.48ms，增量约快 2.37%，因此保留。
这证明直接目标槽在“运行时无形态判定、可在 verifier 固化”的路径上能够产生明确收益。

#### compact 后热块旁路原型（2026-09-14，已撤回）

尝试在 `compact()` 后扫描连续 `ConstantLocal`，为 root/function 指令流附加热块元数据，并在主循环入口旁路
执行该块、跳过逐条 switch 取指。即使候选收紧到无控制流、全部 `discard_result` 的 typed-int 常量初始化，仍使
`bytecode_optimization_test` 和 `register_backend_structure_test` 失败；完整撤回后 Debug x64 恢复 89/89。

原因是连续 opcode 不是完整执行契约：verifier 还隐含维护寄存器高度、目标分配、scope 生命周期和 remap 后的
控制流入口。`compact()` 后从 opcode 序列反推 basic block 会遗漏这些状态，逐条复制 handler 也会形成第二套
难以验证的解释器。后续若做紧凑热码，必须由 verifier 显式产出 block 的入口/出口寄存器状态、唯一入口和完整
副作用描述；在此之前不再尝试 compact 后扫描或旁路执行现有 `Instruction`。

#### 后续减少复制实验（2026-09-13）

按 opcode 统计的 4,493,664 次 `RegisterSlots::copy()` 中，`LoadLocal` 为 3,213,763 次、
`RegisterSnapshot` 为 1,279,019 次、`StoreLocal` 为 882 次；值类型为 4,063,185 个整数、
429,576 个 double 和 903 个资源，且全部发生在同一底层槽存储中。首次尝试让寄存器表达式在编译期
直接引用常量和局部槽、仅为内部结果分配临时槽时，Debug 完整回归从 77/77 降为 74/77，候选当时撤回。
后续完成 Lua 式直接操作数时确认，真正缺陷不是快照承担诊断：混合的临时/常量/局部操作数路径没有服从
`temporary_destination`，嵌套结果被错误写入普通输出槽。统一结果槽后，局部叶子 `SourceRef` 已足以在
消费指令中保持未初始化错误正文和精确位置，最终可安全删除全部寄存器表达式叶子快照，见下节。

随后在 `RegisterSlots::copy()` 内按 tag 直接调用整数、double、bool 写入，避免先复制完整 variant 再
解包标量。Debug 为 77/77，但无插桩 Release A/B/B/A 中，冻结 variant 基线两批优化 VM 中位数平均
93.316ms，候选 94.364ms，退化约 1.12%；四批均为 14 次 PI 输出一致、长度 502。该候选已撤回，
说明 STL variant 的普通复制已足够便宜，额外 tag 分支不值得。

本轮保留两项不增加热路径分支的复制收紧：`RegisterSlots::payload(size_t)` 改为返回槽内 const 引用；
数组声明检查空维度也改用引用型 payload，避免观察值时复制 72 字节 `CompactValue`。最终 Debug 完整
回归 77/77。数组 Profile 中 `array_import_move=0` 并非右值链断裂：`import_object(Object&&)` 已正确
移动 `Object::value`；主要 const 导入仍来自隐式全局数组在宿主表和 VM 槽之间的同步，只能通过完整的
权威全局槽迁移消除。

#### Lua 5.4 字节码对照与直接操作数（2026-09-13）

Lua 5.4 的 `Instruction` 是 32 位无符号整数，opcode 占 7 位，主要使用 iABC、iABx、iAsBx、iAx、
isJ 五种紧凑格式。编译器用 `expdesc` 保留表达式当前是局部寄存器、常量、可重定位结果还是固定寄存器；
`VLOCAL` 可直接成为寄存器操作数，常量可成为 K/立即数操作数，只有目标寄存器不同才发 `OP_MOVE`。
算术还有 `OP_ADDI`、`OP_ADDK` 等立即数/常量专用编码。相比之下，Cifa 的通用 `Instruction` 当前包含
多个 `size_t`、三个源码引用和写入元数据，寄存器二元操作另用 16 字节 `RegisterOperation` 及旁表；
因此 Lua 的执行码明显更紧凑，但此前 Cifa 将热字段拆旁表或简单缩排 `Instruction` 均实测变慢，不能只按
结构尺寸照搬。参考：<https://www.lua.org/source/5.4/lopcodes.h.html>、
<https://www.lua.org/source/5.4/lcode.c.html>。

本轮先采用 Lua 表达式描述符的直接操作数思想。`emit_register_expression()` 现在返回
`{slot, constant, temporary}`：常量直接引用常量表，局部叶子直接引用局部槽，只有内部二元结果占临时槽；
`RegisterBinary` 的数值和 fallback 路径统一读取常量、局部或临时来源，并统一服从
`temporary_destination`。局部读取失败仍使用叶子 `SourceRef` 报告未初始化变量，因此错误正文、源码行和
`^` 位置不变。Debug x64 完整回归为 77/77。

#### 编译器直接目标槽与直接返回（2026-09-14）

将匿名的寄存器表达式操作数提升为编译器私有 `ExpressionDescriptor`：显式区分
`Constant`、`Local` 与 `Temporary`，并随 descriptor 保留 `SourceRef`。这只重构编译期表达式状态，
不另建执行码流，也不更改最终 `Instruction` 的 ABI。赋值端通过 descriptor 在发射阶段直接指定
`RegisterOperation.destination`；叶子与嵌套整数二元表达式的局部声明/`=` 均生成
`NumericBinaryLocal`，不再依赖生成后扫描 `NumericBinary` 的反向补写。整数常量局部声明/赋值直接生成
`ConstantLocal`，旧 `Constant` + `StoreLocal` 融合仅保留给尚未迁移的通用回退路径。

返回端新增 `ReturnLocal` 与 `ReturnConstant`。前者从局部槽（含 alias 和未初始化检查）直接写入调用者
返回槽，后者从常量池直接写入同一槽；两者均保留函数返回类型转换与 `NoValue` 调用结束协议。顶层
`ReturnConstant` 必须以 `CompactValue::export_storage()` 导出公开 `Object::Storage`；直接构造
`Object(CompactValue)` 会选择泛型构造函数并错误存入 `std::any`。结构回归要求：局部结果函数生成一个
`ReturnLocal` 而无普通 `Return`，常量结果函数生成一个 `ReturnConstant` 而无 `Constant`/`Return`。
Debug x64 完整回归为 89/89。

这轮没有冻结改造前后的 A/B 基线，因此不得把指令减少或回归通过表述为性能提升。下一步应将
descriptor 扩展到比较/分支和调用参数窗口；成员、索引、动态 alias、容器写入与复杂返回表达式继续走
现有通用指令，直到能够保持左到右求值、类型转换及诊断顺序。

独立 `CIFA_VALUE_PROFILE` 显示 `RegisterSlots::copy()` 从 4,493,664 次降为 3,214,645 次，精确删除
1,279,019 次 `RegisterSnapshot` 复制，约 28.46%；剩余来源为 `LoadLocal` 3,213,763 次和
`StoreLocal` 882 次。无插桩 Release 三组 A/B/B/A 共十二批中，每批均为 14 次 PI 输出一致、长度 502；
六批基线中位数平均约 95.194ms，六批候选约 94.765ms，候选约快 0.45%。单组波动范围较大
（首组慢 1.11%，后两组分别快 1.35% 和 1.11%），因此只认定无性能回归并保留架构简化，不把 0.45%
作为稳定提速。

下一步若继续借鉴 Lua，应优先让紧凑 `RegisterOperation` 自身编码操作数来源和常用立即数，减少
`RegisterBinarySite` 的布尔判断与旁表读取；必须保持当前统一结果槽语义并做独立 A/B。不要直接把整个
通用 `Instruction` 压成 32 位，因为 Cifa 的动态类型、精确源码诊断、宿主 ABI 和复 Lua 不同。

2026-09-13 尝试引入 VM 私有 `ValueArray = vector<BytecodeValue::Storage>`：纯数组字面量递归转换，
并迁移 `push_back`、单维索引读写、`size`、range 快照和通用数组方法；typed array、map 及带元数据元素
仍保留 `ObjectVector`。结构回归验证了嵌套数组边界往返、深复制和元数据回退，Debug 完整回归 77/77，
原样 PI 结果为 length=502、fnv1a32=1d4b4c2f。无插桩 `/O2 /MD /DNDEBUG` A/B/B/A 中，优化 VM
基线中位数为 91.696/92.225ms，候选为 95.929/97.807ms；两批平均 91.961ms 对 96.868ms，
退化约 5.34%。未优化 VM 基线平均 152.967ms，候选 157.751ms，退化约 3.13%。该实现已完整撤回。

本次失败说明 `std::any<vector<variant<...>>>` 仍不是 Lua 式紧凑容器值：每个元素继续承担完整 variant 尺寸，
执行点还要同时探测 `ValueArray`/`ObjectVector`，边界递归转换也扩大代码与分支。下一版若继续容器槽化，
应先设计单一的紧凑 tag/payload 元素和唯一数组表示，将类型约束放在容器描述中；不能继续在 `std::any`
内并列两种数组并给每个方法增加双分派。

### 6. 生命周期与搬运优化

- [x] 退出窗口将实际槽恢复为 `monostate`，资源在退出时释放；数值槽复用同一清理协议。
- [ ] 根据活跃区间和最后一次使用确定复制、移动与槽复用，避免旧值泄漏或提前释放。
- [ ] 删除无用表达式结果、重复清槽和中间结果搬运，尽可能直接写最终目标。
- [x] 范围快照、返回值和宿主回调的资源生命周期有专门回归，不把释放任意推迟到下次覆盖。
- [ ] 统计真实复制、资源释放、分配和峰值存活量，不把接口调用计数当作总构造或析构次数。

### 7. 指令布局与类型专用执行

- [ ] 将热路径执行字段与源码诊断信息分离，压紧指令布局。
- [ ] 已知类型生成专用运算和转换指令，减少重复运行时类型判断。
- [ ] 编译期确定参数区域、偏移与转换方式，减少调用阶段分派。
- [x] 基于热点测量增加融合指令，配合临时值消除；暂不优先引入 JIT 或复杂函数特化。

### 实施顺序与验收

1. 将局部槽、声明类型、别名和赋值目标在编译期分类，生成无需运行时重新判定的专用指令。
2. 设计唯一的 VM 数组表示，数组元素使用同一种 tagged value；只在宿主 ABI 边界转换 `ObjectVector`。
3. 缩小脚本调用帧，使参数直接占用被调函数窗口，帧主要保存 PC、窗口基址、返回槽和作用域基址。
4. 指令布局暂不继续调整；只有出现明确的取指瓶颈证据后，才重新设计单流编码。
4. 每个完整协议切片执行 Debug 回归；重构期的微小切片使用针对性的结构断言或最小探针。性能比较推迟到
	编译器格式和主发射路径冻结后。

- [x] Debug 完整测试及递归、参数嵌套调用、宿主重入、类型转换、别名和容器生命周期回归通过。
- [x] 错误正文、实际源码行及 ^ 对齐同时核对，保持现有诊断行为。
- [ ] 数值循环、函数调用、容器复制、宿主交互分别测量，不能只用 PI 判断全部收益。
- [x] 计数构建与耗时构建分开；比较保留校验值、多次样本及环境说明，优先交错 A/B。
- [ ] 内部不再存在兼容 Object 执行；脚本调用不再单独分配参数与局部槽数组。
- [ ] 已绑定普通变量访问不再按名称查表；已知数值操作由专用指令直接读写统一槽。

历史参考而非新架构结果：Debug 76/76；PI 优化 VM 中位数 122.198ms，length=502，fnv1a32=1d4b4c2f。
各阶段更新实测数据和勾选状态，不以接口名字消失或单一性能样本作为完成依据。

## IDBA 与统一窗口历史阶段（2026-09-13，IDBA 已撤销）

### 已落地并通过 Debug 的部分

- Bytecode 优化现已默认开启。模块记录实际执行 Session 的脚本函数版本，首次发布自身函数后同步到发布后版本；
	后续独立脚本仍可调用或发布持久函数，活动优化模块执行期间通过 `run_string`、`run_file` 或宿主回调替换脚本函数则报错。
	Bytecode 覆盖两个内建嵌套执行函数，使其使用共享 Bytecode Session，而不是静态绑定到 AST 执行路径。
	优化模块的宿主版本检查统一对照 Session 的真实宿主。需要执行期间动态替换宿主或脚本函数时，显式使用
	`set_optimization_enabled(false)` 兼容模式。
- AST 前端的 `break`、`continue`、`goto` 已从 `Object::type1` 特殊字符串迁入 `ExecutionContext::ControlFlow`；
	循环按层消费 break/continue，switch 只消费 break，continue 继续传播到最近外层循环，代码块按本层标签消费 goto。
	Bytecode 编译 continue 时同样反向选择最近的非 switch 循环，不再把 switch 内 continue 编译为特殊 Object 常量。
	静态检查中无写入来源的 `type1 == "__"` 旧哨兵已删除，Bytecode 编译期常量标记也不再从 Object 特殊类型推导。
	`Object::type1` 仅保留 `Error`、`NoValue` 等值／API 边界描述，不再驱动 AST 运行期跳转。
	新增双后端控制状态回归，switch 内 continue 的 C 语义结果由错误的 56 修正为 46，并通过 Debug 77/77。
	静态检查同时拒绝顶层 break、顶层 continue 和仅位于 switch 内的 continue；允许 switch 内 break 及 loop+switch 内 continue。
	双后端回归同时核对错误正文、源码行和 `^` 对齐。当前完整 Debug 回归为 77/77。
- `Machine::registers` 持有执行存储。形参、静态局部、临时值、内部返回槽使用同一底层存储；
	每次脚本调用不再创建独立局部负载数组。稳定窗口描述放在 deque 中，调用帧记录窗口恢复信息。
- `ScopeEnter` 编译时记录本层静态绑定数量，执行时一次预留 `Scope::bindings` 容量，避免循环块绑定逐级扩容。
	不含动态槽的退出 Scope 会进入执行函数内回收池，保留 bindings 容量供后续循环迭代复用；含动态槽的 Scope
	仍按原生命周期析构。`ScopeLeave` 和 break/continue 的 `Unwind` 共用回收路径，函数帧切换仍保持独立作用域栈。
- `CallBegin` 在实参求值前预留形参及局部窗口，`BindArgument` 直接转换到预留形参槽。
	嵌套调用和宿主重入使用更高窗口；返回及错误早退恢复位置，保留底层容量并立即释放退出资源。
- switch 条件、range 待绑定值、range 进入时的数组深复制快照、容器方法参数均已迁入主执行窗口；
	验证器分别统计最大同时活动数量并预留槽区。range 在结束和 Unwind 时释放快照，方法参数按 LIFO 回顺序回收。
	执行期辅助状态不再创建独立 `RegisterSlots`；当前独立槽分配只剩真正的动态作用域存储。
- 本阶段曾将纯槽负载拆成连续 `int64_t`、`double`、字节 bool 和独立资源区，逻辑槽描述为区域和偏移，
	当时 `sizeof(BytecodeValue)=16`。2026-09-13 对照 Lua 后确认这会增加二次寻址、平行容量和空闲链维护，现已撤销。
- 当时 A 区使用稳定堆对象保存资源 variant，并复用区域空闲偏移。当前资源直接位于槽内 `Storage`，
	不再承诺寄存器 vector 扩容后槽内地址稳定；外部只允许保存槽号或窗口偏移。
- 名称文本池与槽名称编号分离，普通 int/double 形参可直接绑定数值约束，不因名称强制物化。
	数值二元运算、转换、纯槽赋值、复制、自增已有直接区域标量读写路径。
- 类型约束与特殊标记使用共享描述池和每槽编号，支持跨存储复制、移动及边界还原。
	参数、返回、显式强转、无别名普通局部声明和赋值、范围变量绑定、作用域名称读取已接入槽接口。
- 常量池改为内部负载与 continue 标记；容器常量的元素仍使用 Object。
	条件、字符串拼接比较和 NoValue 二元错误直接读负载与来源描述。
- 二元槽回退直接分派注册操作符，仅在回调 ABI 边界导出临时 Object，不再调用 reference 或 Machine::binary。
	覆盖无回调、空结果继续匹配、同槽双参数、结果覆盖输入及浮点整数运算错误优先级。
	无别名及作用域别名的复合赋值、自增和融合写回已接槽接口；融合计算不再执行 Object 数值回退。
- 一元正负号、逻辑非、位非、switch 比较直接使用槽。switch 的 NoValue 比较在读取 bool 前检查错误，
	避免错误 variant 访问；新增双模式无副作用回归。
- 整数索引、map 字符串键和方法参数转换直接读取槽诊断；数组 contains 的内建比较直接读负载。
	数组和结构体局部声明、map 成员及通用索引首层访问不再要求基容器物化，元素仍是 Object。
- 兼容源 copy/move 经导入接口拆分负载与元数据，新目标不继承兼容存储；已暴露的源引用保持地址稳定。
	新增类型描述、名称、移动清空、容器约束及成员访问的结构断言。
- 已为模块名称表建立执行期全局对象链接：普通读取、局部别名读取、融合快照及融合回退共享该链接；
	局部作用域仍优先遮蔽。链接只指向宿主 `global_variables` 中唯一 Object，不镜像可变值；
	新增宿主注册大量变量触发哈希扩容、替换同名全局、局部遮蔽恢复和重复 Session 的双模式回归。
- 全局、数组元素和结构体字段的复合写入及前后置自增已改为临时槽计算，再在目标边界赋值。
	`write_value` 与旧 `Machine::binary` Object 二元执行实现已删除；数组、map 和结构体元素本身仍是 Object。
- 局部 `IncrementLocal`、`StoreLocal` 和三类融合二元写回已将作用域声明冲突解析为明确的文件/槽目标；
	活动局部别名也优先直接写绑定槽。生产执行路径不再调用 `RegisterSlots::reference()`，Profile 分类读取槽描述和负载，
	不会因插桩物化被测值。`reference()` 目前仅封装在尚待收缩的 `Machine::find_object/get_or_create` 旧接口中。
- `PrepareStore` 的局部变量预绑定、普通/复合写回、全局读取和成员 fallback 已拆分为槽目标或宿主全局边界；
	`assign_named` 与模块 `alias_target` 不再搜索局部 Object。方法参数错误也直接生成与 `call_method` 一致的诊断，
	不再借 `Object::ref` 副作用触发错误。`indexed` 对局部数组声明和未初始化局部容器直接写 A 区资源负载，
	宿主容器分支直接使用全局 Object；`Machine::get_or_create/find_object` 已删除。
- `RegisterSlots::reference()` 已删除。后端测试不再主动锁定“整槽物化后地址稳定”的旧契约，改为验证 A 区资源
	跨逻辑槽扩容地址稳定、资源深复制/移动、窗口恢复及时释放、名称边界往返和槽类型约束。
	`CIFA_VALUE_PROFILE` PI 结果为 length=502、fnv1a32=1d4b4c2f，`metadata_allocations=0`、`object_copies=0`；
	优化 VM 中位数 121.446ms。
- `argument_origin` 已从兼容 Object 迁入独立旁侧数组；Object 导入始终拆分为 IDBA 负载、类型、名称和来源描述。
	`RegisterSlots::objects` 字段及所有双轨读取已删除，复制、移动、赋值、类型绑定、转换、条件、范围、索引和成员负载
	只读取槽区与旁侧描述。宿主回调和公开结果边界按需构造短生命周期 Object，不回写为内部长期值源。
	字段删除后的 Debug 完整回归为 76/76。
- 无元数据的 int/double/bool 数组元素读取直接写 IDB 槽；无元素类型约束、无名称和来源描述的数值 `push_back`
	直接构造容器元素并清理参数槽，跳过通用 Object 元数据导入导出。Value Profile PI 中 `object_imports` 从
	4,634,903 降至 4,211,949，`object_exports` 从 4,760,210 降至 4,339,062；`metadata_allocations=0`、
	`object_copies=0` 保持不变。数组、map 和结构体元素的所有权仍是 Object，本项只是边界快路径，不代表容器元素槽化完成。
- 脚本函数无返回值时直接写 A 区 `NoValue` 负载和特殊描述，不再构造临时 Object；
	已物化兼容数值目标直接更新稳定对象，不再重复调用 `reference()` 切换值源。
- 解析器的后置 `++/--` 已支持单层成员与索引赋值目标，不再只接受裸变量；
	`counter.value++`、`items[0]++` 的 AST 与字节码双模式、重复执行对照已通过。
- Debug 完整回归 76/76。新增断言覆盖真实区域元素、A 区引用跨扩容稳定、区域复用、
	自移动、嵌套实参、递归、重复执行容量保留及清空、真实 Session 宿主重入、名称边界往返、
	全局链接扩容稳定性，以及全局／数组／结构字段复合写入。
	新增方法参数中嵌套方法调用，以及嵌套 range、break、循环中修改原数组的快照语义回归。

### PI 实测

测量使用 MSVC x64 `/O2 /MD /DNDEBUG`，不插桩，运行原样 `cifa/calc-pi.c`。修改前后的版本按 A/B/B/A
顺序交错运行，每个进程预热 1 次、测量 6 次。
所有执行结果均与 AST 完整字符串比较，length=502、fnv1a32=1d4b4c2f。

| 批次 | 基线 A1 | 当前 B1 | 当前 B2 | 基线 A2 |
| --- | ---: | ---: | ---: | ---: |
| 优化 VM 中位数 ms | 128.535 | 121.610 | 119.235 | 129.024 |
| 未优化 VM 中位数 ms | 188.276 | 185.541 | 189.847 | 190.970 |

第 9/10 批对应兼容源 copy/move 拆分后的源码，早于后续别名、结构体／数组声明、
全局链接及复合写入迁移，因此不是当前最终性能验收。按两批中位数的平均值比较（不是合并样本中位数），
优化 VM 耗时下降约 6.5%，未优化 VM 耗时下降约 1.0%，后者波动较大。
第 11/12 批重建了当时最新的全局链接源码：基线优化 VM 为 125.207/123.800ms，
当前为 118.773/188.914ms。第 12 个当前样本范围为 148.716--197.456ms，明显异常，
仅保留为环境波动记录，不用于结论。
随后的第 13/14 批复测：基线优化 VM 为 122.728/122.807ms，当前为 115.998/116.091ms；
两侧中位数平均分别为 122.768ms 与 116.045ms，优化 VM 耗时下降约 5.5%。
未优化 VM 的对应平均值为 179.913ms 与 180.283ms，基本持平。全部样本均校验
length=502、fnv1a32=1d4b4c2f。
第 11--14 批早于最后删除旧 Object 二元执行和解析器后置成员修复，仍不是最终性能验收。
第 15/16 批重建了辅助状态统一窗口后的最新源码，全部结果校验仍为 length=502、fnv1a32=1d4b4c2f，
但基线优化 VM 自身从此前约 123ms 跳到 229.886/231.482ms，未优化基线样本也呈现约 188ms 与
315--331ms 的明显双峰；当前优化样本同样出现 118--215ms 跨档波动。该批仅作为环境异常记录，
不计算代码改善比例。
局部及融合写回去物化后的第 17/18 批均校验 length=502、fnv1a32=1d4b4c2f。第 17 批优化 VM
中位数为 121.712ms，未优化 VM 为 184.843ms；第 18 批再次出现跨档波动，优化样本范围
142.025--218.575ms，未优化样本范围 184.019--321.503ms，因此仍不计算改善比例。
删除 `RegisterSlots::objects` 并加入数值数组读取／追加边界快路径后的第 21 批，优化 VM 六次样本为
108.248/108.493/108.729/108.971/109.078/109.135ms，中位数 108.971ms；未优化 VM 中位数 169.289ms，
其中一个样本为 183.173ms。结果仍为 length=502、fnv1a32=1d4b4c2f。该批仅记录当前绝对值，
不与此前双峰环境批次计算改善比例。
默认优化和函数冻结修正后的无插桩 `/O2 /MD /DNDEBUG` 基准中，显式未优化 VM 六次中位数为
172.656ms，默认优化 VM 六次中位数为 108.370ms，优化执行耗时约低 37.2%。两组均使用同一原样
`cifa/calc-pi.c`，结果为 length=502、fnv1a32=1d4b4c2f。基准已显式设置未优化组为 false，避免默认值变化后
两组实际都启用优化。
Profile 曾显示 `MethodPush` 181,860 次、插桩耗时约 23.6ms；尝试让局部数组 push_back 直接按帧槽读取后，
同一 Profile 的 `method_push_ms` 升至约 25.2ms、总执行也上升，因此该改动已撤回，不计为优化成果。
Scope 优化按两阶段 A/B/B/A 验证。仅增加编译期 binding 数量与 `reserve()` 后，优化 VM 从两批 A 平均
109.983ms 降到两批 B 平均 102.063ms，约下降 7.2%；未优化 VM 从 172.728ms 降到 166.040ms，约下降 3.9%。
进一步复用纯静态 Scope 容器后，优化 VM 从两批 A 平均 101.450ms 降到两批 B 平均 96.867ms，约再下降 4.5%。
最终独立六次复测：优化 VM 中位数 95.837ms，未优化 VM 中位数 160.628ms；相对本轮 A/B 基线
109.983ms 累计下降约 12.9%。所有样本均为 length=502、fnv1a32=1d4b4c2f。
尝试让 Scope 保留 Binding 对象并用活动前缀避免字符串析构后，优化 VM 从约 97.185ms 退化到约 99.576ms，
约慢 2.5%，该第三步已撤回。最终 Debug 回归 77/77。
Scope 优化后的当前代码重新增加分区 Profile。PI 中 `StoreLocal` 共 437,558 次，其中 typed numeric
248,475 次、numeric assign 180,998 次、compound local 8,084 次；`IncrementLocal` 182,847 次全部走整数快路，
`RegisterSnapshot` 185,109 次中局部槽为 182,717 次。`MethodPush` 181,860 次中，扩容追加 26,457 次，
稳定容量追加 155,403 次；扩容约占追加计时的 80%，但在首次 push 时固定 `reserve(16)` 的无插桩 A/B/B/A 中，
基线两批平均中位数为 96.830ms，候选为 97.269ms，约慢 0.45%，因此该行为已撤回。
为每个 `VariableSite` 增加数值类型字段也使优化 VM 从基线平均 96.405ms 退化到 98.336ms，约慢 2.0%，
已撤回，避免扩大热描述符。
保留的改动是在 Module 级缓存 `int`/`double` 名称 ID，typed numeric 初始化用 ID 判定和 `NumericBinding`
枚举转换，保持 `VariableSite` 布局不变。Debug 完整回归 77/77；无插桩 A/B/B/A 的基线两批平均中位数为
102.415ms，候选为 99.794ms；该批基线自身相对此前约 96ms 档明显漂移，只用于同批相对观察。
随后按 A/B/A/B/A/B 重测，三批基线中位数为 96.784/96.859/96.937ms，三批候选为
95.330/94.416/94.979ms；三批中位数平均由 96.860ms 降至 94.908ms，约下降 2.02%。两侧各 18 个执行样本的
合并中位数分别为 96.761ms 和 94.781ms，当前绝对性能已回到并略优于此前 95.837ms 档。
分区 Profile 中 typed numeric 从约 11.96ms 降至 8.28ms，
`StoreLocal` 总计从约 26.10ms 降至 22.49ms；numeric assign 仍约 13.54ms，未随本项变化。所有 PI 结果均为
length=502、fnv1a32=1d4b4c2f。
下一步按当前数据先研究 numeric assign 与 `RegisterBinary` 写回的重复读取／结果复制；随后再评估数值数组的
内部紧凑元素表示。固定小容量 reserve 和扩展 `VariableSite` 字段均已证伪，不重复尝试。

后续按上述顺序完成了候选验证。`RegisterBinary` 共 584,768 次：temporary result 63,163 次、
temporary numeric assign 60,243 次、local result 459,489 次、local numeric assign 1,873 次，
其余通用赋值和 fallback 在 PI 中均为 0。62,116 次 numeric assign 全部是 typed `int` 初始化，且结果全部丢弃。
尝试让临时二元结果直接写 typed int 局部槽后，Debug 77/77、PI 校验正确，但无插桩 A/B/B/A 从
94.409ms 退化到 98.806ms，约慢 4.66%，已完整撤回。

尝试用 VM 私有 `ValueArray`（`vector<BytecodeValue::Storage>`）承载无类型数值数组，并接入空数组、
`push_back`、`size`、索引读取和宿主导出物化。Debug 77/77、PI 校验正确，但无插桩 A/B/B/A 从
94.086ms 退化到 98.157ms，约慢 4.33%，已完整撤回。`std::any` 双容器分派和 Storage variant 元素没有
优于现有 ObjectVector 数值构造快路，后续若重做紧凑数组，应采用单一数组表示或独立 tag，不再在 `std::any`
中并列探测两个容器类型。

全局链接 Profile 显示 PI 中仅 15 次首次字符串查找、3,997 次已链接指针命中、0 次全局创建，
`read_alias` 的局部／全局／动态三条路径均为 0；正式 Module/Session 全局槽链接不是当前 PI 热点，暂不改造。

尝试将 switch、range 和 method argument 执行状态改为编译期定长 `vector<optional<State>>`，并为
switch/range 编码稠密状态槽。Debug 77/77、PI 校验正确，但无插桩 A/B/B/A 从 93.024ms 退化到
94.118ms，约慢 1.18%，已完整撤回；固定向量初始化和函数帧搬运成本高于当前小型 unordered_map。

指令布局 Profile 显示 `Instruction` 为 104 字节，PI 执行约 3,993,553 条；source 几乎每条使用，
operand 3,391,622 次，inputs 1,465,560 次，discard 903,965 次，member_site 为 0。将 Opcode/WriteOperation
收窄到 8 位并重排字段后结构降至 96 字节，Debug 77/77、PI 校验正确，但同批 A/B/B/A 从 93.388ms
退化到 101.606ms，约慢 8.8%，说明高频字段局部性比结构体总尺寸更重要，已完整撤回。

本轮最终源码仅保留 `CIFA_VM_PROFILE` 下的 RegisterBinary 分区、全局链接和指令字段密度统计；正常构建
不含上述候选行为变化。最终撤回状态 Debug 77/77，PI length=502、fnv1a32=1d4b4c2f。最终单批绝对中位数
98.037ms 处于环境慢档，仅作为正确性记录；各候选结论均来自同批交错 A/B。

下一轮不再优先做局部二元直写、`std::any` 双容器紧凑数组、固定执行状态向量或简单 Instruction 字段重排。
需要先确认正常构建下的性能热点；结构性方向应优先研究保持热字段前缀不变的冷热指令旁表，
或让数组从创建起使用单一 tagged 存储，避免运行期双类型探测。
此前批次 3/4 的约 9.9% 和批次 5/6 的约 6.8% 是中间源码结果，不作为当前结论。
中间版本曾因逐槽名称哈希和 variant 数值往返退化到约 137--141ms，已用名称编号和直接数值路径修正。

### 撤销四区 IDBA，恢复统一值槽（2026-09-13）

对照 Lua 5.4 的 `TValue`、连续栈、32 位指令、`CallInfo` 和 table array/hash 设计后，确认 Cifa 的四区 IDBA
只缩小了逻辑槽描述，却使每次数值访问经过 `region/offset -> typed pool` 二次寻址，并维护四类容量、空闲偏移和
资源间接所有权。此前四个局部候选均未提速，说明继续围绕该布局减少单次 copy 不是可靠方向。

当前 `BytecodeValue` 已恢复为每槽直接内联一个 `Storage` variant，删除 `Region`、`offset`、`integers`、
`doubles`、`booleans`、`resources` 和 `free_offsets`。名称、类型、数值绑定和参数来源旁表保持不变；
数组、map、结构字段和宿主 ABI 仍保持原有 Object 语义。同文件 `move` 在移动 variant 后显式把源槽恢复为
`monostate`；内部测试不再要求槽地址跨 `vector` 扩容稳定。

验证结果：VS18 `Debug|x64` 完整回归 77/77，错误正文、源码行和 `^` 对齐用例均通过；Debug 下
`sizeof(BytecodeValue)=72`、`sizeof(Object)=248`。Release `/O2 /MD /DNDEBUG` PI 结果为 length=502、
fnv1a32=`1d4b4c2f`，优化 VM 六次样本 91.428--94.729ms，中位数 92.155ms；历史稳定 IDBA 中位数
94.908ms 来自另一批环境，本次只能确认没有明显退化，不能宣称严格提升。

后续方案按架构收益和风险排序：

1. 编译期槽语义：为已证明的局部数值初始化、赋值和运算生成专用 opcode，热路径不得再判断声明、别名和绑定状态。
2. 单一 VM 数组：从创建起只使用一种 tagged element 存储，整数索引直接返回元素槽；宿主调用边界才转换 ObjectVector。
3. 精简调用帧：参数直接进入被调函数窗口，帧只记录恢复所需偏移和 PC；辅助状态使用帧内 arena/top，不移动多组容器。
4. 指令编码：当前不实施独立执行数组；待采样证实取指瓶颈后，再考虑不复制 code、不增加旁表访问的单流编码。

禁止重复的方向：重新拆分 I/D/B/A 池、在 `std::any` 中并列探测第二种数组、仅靠字段重排压缩现有 104 字节指令、
或在现有通用 `RegisterBinary` 内继续增加运行时直写判断。

### 紧凑执行指令 A/B 证伪（2026-09-13，已撤回）

尝试保留 104 字节编译/校验 `Instruction`，在 `compact` 后 lower 到独立执行数组，并用 PC 旁表保存诊断源码。
第一版为 40 字节热指令，`source/condition/target` 全部旁侧化；第二版将几乎每条指令都读取的 `source` 放回热流，
热指令为 44 字节，仅旁侧化 `condition/target`。两版均有 32 位索引范围检查，Debug 77/77，PI length=502、
fnv1a32=`1d4b4c2f`。

同一冻结统一槽基线 A/B/B/A：40 字节版基线两批中位数平均 93.175ms，候选 95.870ms，退化约 2.89%；
44 字节版基线 92.848ms，候选 94.972ms，退化约 2.29%。两版均已完整撤回。结果说明本工作集不能仅靠
复制一份紧凑执行数组获益；额外 lower、双份代码工作集及旁表访问抵消了结构缩小。下一步改做编译期 opcode 语义专用化。

### TypedStoreLocal 专用 opcode A/B 证伪（2026-09-13，已撤回）

尝试在编译期把显式 `int/double` 局部普通赋值标记为 `TypedStoreLocal`，跳过运行时对 `write`、`with_type` 和
`type_id` 的重复分类；作用域同名冲突仍回落原 `StoreLocal`。Debug 77/77，PI length=502、
fnv1a32=`1d4b4c2f`。同一冻结基线 A/B/B/A 中，基线两批中位数平均 92.975ms，候选 93.483ms，
退化约 0.55%，已完整撤回。仅拆出分类 opcode 没有减少 `initialize_numeric`、scope 查询、绑定和结果搬运，
收益不足以抵消额外分派。后续专用化必须覆盖完整数据流，或先用正常 Release 性能数据确认具体指令级热点。

### 仍未完成

- `RegisterSlots::objects/reference` 与 `Machine::get_or_create/find_object` 已删除；宿主回调 ABI 仍需边界 Object，
	数组元素、map 值和结构体字段本身也仍是 Object，属于后续容器内部值槽化范围。
- 当前 `Object` 实例字段没有可无损直接删除项：`value` 是公开负载；`bound_type`、`declared_type_name`、
	`element_type_name` 分别承担声明类型、struct/注册类型和容器元素约束；`name` 用于变量诊断；
	`argument_origin` 保证宿主回调复制参数后仍能把转换错误定位到原脚本实参，现有双后端回归覆盖该行为；
	`type1` 已收敛为 `Error`/`NoValue`，但双字符串构造器与 `getSpecialType()` 属于公开 API。后续若要缩小 Object，
	应先设计兼容的特殊值枚举和独立参数来源上下文，不能只删除字段。
- AST 的值执行接口仍按值返回完整 Object，变量读取和类型透传会复制数值及名称／类型元数据；
	单靠返回值消除或改 `std::move` 不能解决。当前决定暂不优化 AST 执行路线，性能工作继续集中于 Bytecode VM。
- 当前统一槽仍使用 72 字节 `Storage` variant；已知类型指令尚未消除运行时声明、别名和赋值目标判断。
- 动态作用域仍有独立槽存储；范围快照、switch 条件及方法参数已使用主窗口。全局读取及别名写入已有执行期模块名称编号链接，
	但索引、成员、声明和动态名称路径尚未完整接入，编译／装载期正式链接与失效策略也未完成。
- 历史 16 字节 IDBA 只是逻辑描述，完整内存还包括四个负载池、空闲偏移、名称编号、来源指针和容量余量。
	当前 72 字节统一槽消除了这些附加池；后续需测峰值工作集和缓存缺失，不能只比较 `sizeof`。
- 尚需完成容器元素迁移、全局正式装载期链接、逐项核对错误正文/源码/^、补分类性能与最终验收。
	总计划不能按上述阶段通过就整体勾选完成。

## 统一窗口前置修正（2026-09-13，历史阶段）

- 当前 `RegisterSlots::restore` 原先只恢复窗口游标，退出窗口的负载会留在保留容量内，
	直到再次覆盖或整个槽文件销毁。本次恢复前清理退出范围，释放其中实际持有的资源。
- 保留槽数组容量和已物化对象的地址；外层窗口的值不被清理。此处仍使用旧槽布局，
	不是 IDBA 数值区免析构方案，也不是 Object 兼容路径删除。
- 在现有 `RegisterBackendTest` 中增加嵌套窗口资源、已物化资源立即释放、外层存活、
	容量保留和复用后槽为空的断言。x64 Debug 重建及完整回归通过 76/76。
- 未测性能，因此未声称提速。
- 后续仍需将调用前窗口预留、形参诊断描述分离、局部和临时存储统一与 IDBA 布局共同推进。
	当前脚本调用仍创建独立局部 RegisterSlots，参数仍物化 Object；总体计划各项保持未验收。

## 内部状态与容器槽迁移（2026-09-13，尚未完成全部去 Object）

- 动态作用域存储改为 RegisterFile；Binding 保存文件和槽编号，移除 Object deque、Object lookup/create 返回协议。
- 返回状态移除 Object 和未使用标志；范围快照、待绑定范围值、方法参数均使用寄存器文件。
	范围快照仍保留全部元素直到范围结束，不能消费快照改变资源析构时机。
- 删除闲置 binary_values 和 Machine::push_back；read_named 直接填写结果槽。
- 条件数值及空值检查、一元数值负号、容器长度直接操作负载。
- 容器方法接收负载与元素类型约束，参数和返回值使用槽，删除 Object 参数回调和返回协议。
	专用 push 和数组快速读取不再强制物化接收者；通用索引直接读取键槽，不建立 Object 键数组。
- 普通数值声明和融合算术声明共用直接目标槽初始化，避免先建立动态 Object 再迁移到静态局部槽。
- 非数值融合声明同样直接绑定当前层局部槽；命名 size 及数组快速写入不再物化接收者。
- BytecodeValue 独立声明 Storage；此项仅整理类型归属，不代表运行期 Object 已移除。

Debug 完整回归 76/76。新增动态槽扩容后绑定稳定、重复创建不增加槽、条件及容器方法不物化接收者的断言。

独立 CIFA_VALUE_PROFILE 的 PI 一次执行结果：

| 事件 | 次数 |
| --- | ---: |
| object_imports | 219,409 |
| object_exports | 185,869 |
| reference 请求 | 12,328 |
| reference 物化 | 2,820 |
| 元数据/移动物化 | 3,278 |
| copy | 645,566 |
| copy 兼容来源 | 65,112 |
| clear | 4,541,518 |
| clear 兼容槽 | 2,735,687 |

中途发现融合声明仍创建动态槽，首次物化曾升至 65,411；改为直接目标槽绑定后才降至 2,820。
复制分配的统计仍不完整，以上不能作为总构造、总分配或总析构次数。

未插桩 Release 六次：优化 VM 中位数 122.198ms，样本 121.676/121.780/122.088/122.198/122.934/123.177ms；
未优化 VM 中位数 177.631ms。与上一批 153.845ms 相比下降约 20.6%，属于跨批次比较，不是严格交错 A/B。
PI length=502、fnv1a32=1d4b4c2f；Release Object=216、Storage=72 字节。

**未完成项：** RegisterFile::objects/reference 仍存在；脚本参数绑定、通用类型绑定/转换、复杂赋值、
多维索引接收者、容器元素和常量仍依赖 Object；宿主全局名称表仍为哈希表。
因此“全部变量绑定寄存器、内部只留槽、Object 仅用于宿主边界”的整体要求尚未完成。

## 槽执行迁移与旧接口删除（2026-09-12）

RegisterFile::read/take/write 的声明、实现及字节码调用已删除，CifaBytecode.h/cpp
搜索这三个函数调用无匹配，Debug 完整回归76/76。测试改为直接检查 payload，增加原位转换、
越界转换不修改目标、槽移动和源失效检查。

实际迁移（不是仅改函数名）：

- 数学一元/二元数值指令、数值分支、整数位非及逻辑非直接读写槽。
- 普通常量直接加载payload；非别名变量快照直接复制槽，保留副作用之前的快照。
- 纯值copy直接复制payload与NumericBinding；move直接移动槽并清理源，不生成返回Object。
- 数值初始化、复合赋值、强转与参数预转换使用槽操作；非法转换保留诊断回退。
- 脚本参数跨寄存器文件移动；内部返回用固定结果槽跨窗口移动，不经过ReturnState::value。
- switch条件保存在独立槽；数值case直接槽比较。数组快速索引直接提取槽中整数。
- 范围条件、容器尺寸和数组负载直接写槽。宿主参数、数组元素及顶层结果直接填充最终Object位置。

仍有兼容边界：import_object(const Object&/Object&&) 直接复制或移动到最终存储，
export_argument 填充调用者提供的Object位置；不再提供按值read/take/write接口。
这两个边界并非仅用于外部宿主：通用类型转换、复杂赋值、容器元素及自定义运算仍使用Object。
reference也仍会物化兼容对象。接口已删除不等于所有变量已静态绑定、不等于纯槽执行覆盖所有语义；
动态名称/宿主全局路径仍待迁移，不能将本次描述为整个VM完全去Object化。

计数开关CIFA_VALUE_PROFILE重新定义口径，旧reads/takes/writes不再输出，避免三个零误导：

| 事件 | 当前PI一次执行 |
| --- | ---: |
| object_imports | 471,362 |
| object_exports | 185,869 |
| reference请求 | 603,090 |
| reference物化 | 1,421 |
| 元数据/移动物化 | 3,277 |
| copy | 641,826 |
| copy兼容来源 | 243,904 |
| clear | 3,708,464 |
| clear兼容槽 | 2,972,287 |

copy兼容来源统计已修正：带NumericBinding的纯值复制不再算Object路径。
兼容来源copy新建目标对象尚未纳入物化计数，因此1,421+3,277不是本轮完整分配总数；
以上不是全部构造/析构计数，也不能与旧接口计数简单相减得出对象减少量。

未插桩Release：优化VM中位数153.845ms（152.715--154.315），未优化VM226.761ms。
迁移前199.572ms，跨批次下降约22.9%，并非交错A/B；AST本次有532.782ms离群值，
不使用其均值计算速度比。PI始终length502、fnv1a32=1d4b4c2f。
后续应按内部语义拆除兼容边界，而不是追求统计接口名称消失或声称元数据复制是唯一瓶颈。

## 剩余 Object 审计（2026-09-12）

测量配置：MSVC x64 Release /O2 /DNDEBUG，单独定义 CIFA_VALUE_PROFILE，使用
使用单独的 profile build 编译并运行一次优化后的 cifa/calc-pi.c，println 注册为空回调。
不定义 CIFA_VM_PROFILE，后者的部分旧统计会调用 reference 并改变物化行为。
两次执行计数一致，PI length=502、fnv1a32=1d4b4c2f。计数覆盖当前线程进程生命周期，
此探针只编译、执行一次；不是所有 Object 构造函数的全局计数。

| 寄存器边界事件 | 次数 | 含义 |
| --- | ---: | --- |
| read | 1,018,282 | 按值返回 Object，含兼容对象复制或纯值还原 |
| take | 1,273,637 | 消费槽并返回 Object，负载可移动，仍有边界对象 |
| write | 2,209,898 | 接收按值 Object；与 read/take 重叠，不可相加当独立对象数 |
| reference | 593,355 | 请求可修改 Object&；多数重用已物化对象 |
| reference 首次物化 | 7,967 | 实际执行 make_unique<Object> |
| write 元数据物化 | 5 | 实际执行 make_unique<Object> |
| copy | 459,109 | 非自复制的寄存器复制 |
| copy 经 Object 路径 | 459,109 | 100%，包含上面的 read/write，不是额外独立计数 |
| clear | 2,536,697 | 槽重置次数，含 take 和 write_payload 内部调用 |
| clear 兼容槽 | 1,428,588 | 约56.3%，仍清理完整 Object 的元数据 |

Release sizeof(Object)=216，sizeof(Storage)=sizeof(BytecodeValue)=72。
当前每槽还需8字节兼容指针和1字节 NumericBinding；不含 vector 容量余量和分配器开销，
纯值槽约81字节，物化槽约297字节（81+216）。相对原216字节槽，若某一时刻物化比例
超过62.5%，这一混合布局连基本存储都不再节省。此次未统计同时存活槽数及物化峰值，
不能由累计7,972次分配推断实际峰值比例。
7,972次兼容对象分配的对象本体累计约1.64MiB，不含字符串/any负载和分配器开销，
不是驻留内存，也不是全部运行时分配量。

### 影响估计

- read+take 共2,291,919次返回 Object 的边界事件，说明“寄存器已经纯值”没有消除大量中间对象。
	返回值优化、移动和内联会改变实际构造指令，不能把事件数等同于堆分配数。
- copy 的459,109次全部经过完整 Object 边界，是最明确的后续目标：按槽直接复制 payload 和
	NumericBinding；兼容来源必须仍保留特殊类型、NoValue、元素约束及来源语义。
- 约56.3%的清槽仍走兼容元数据清理，比纯数值重置更重；兼容槽一旦物化会保留到寄存器文件销毁。
- 若仅作敏感性估算，每次 read/take 边界净省1/5/10ns，对应总计约2.29/11.46/22.92ms。
	这不是测得的单次成本或提速预测；copy/clear等事件重叠，不能把估算重复相加。
- 对约199.572ms的历史执行时间，这一敏感性区间约1.1%--11.5%。实际收益必须通过移除指定路径后
	的未插桩交错A/B验证，当前计数不足以认定 Object 占用多少百分比CPU时间。

### 仍保留的位置

除寄存器兼容对象外，常量池、动态作用域槽、宿主全局表、返回状态、范围遍历值、参数向量、
数组/map元素及 Machine 运算/类型转换接口仍使用 Object。这些数量随脚本、容器大小和调用深度变化，
不能给出一个通用的“剩余对象总数”。上述寄存器计数不覆盖其独立构造/复制，也不统计AST编译对象。
常量和宿主接口可以在边界保留；优先级应放在运行期每次计算重复发生的 copy、声明转换和清槽，
而不是以源码 Object 关键字数量作为优化目标。本次仅审计和迁移文档，未据计数修改执行语义。

### VM 优化依据与计划（2026-09-12）

对照来源为 Lua 官方 5.4.9 的 [lcode.c](https://www.lua.org/source/5.4/lcode.c.html)、
[lvm.c](https://www.lua.org/source/5.4/lvm.c.html) 和 [lobject.h](https://www.lua.org/source/5.4/lobject.h.html)。
这是架构对照，不是 Lua 与 Cifa 的性能横评。

| 路径 | Lua 的处理 | Cifa 可借鉴的方向 |
| --- | --- | --- |
| 局部写入 | `luaK_storevar` 将局部目标传给 `exp2reg`，可重定向表达式目标；`MOVE` 只搬运值与标签 | 编译期定位槽，减少名称查找和初始化时的中间对象搬运 |
| 算术 | 直接读写寄存器，成功后跳过元方法回退指令 | 已有 `RegisterBinary`，继续关注物化和元数据成本，不重复建设 |
| 表访问 | 整数索引、字段有专用路径，复杂行为走回退 | 保留数组快路径；先细分 `MethodPush`，不能未经测量归因于扩容 |
| 调用与捕获 | 调用切换帧，捕获局部通过 upvalue 间接访问 | 普通局部与需要间接访问的变量分开处理，保留宿主调用边界 |

不能直接照搬 Lua 的 `TValue`：Cifa 的声明类型、注册转换、NoValue、Object 元数据和容器值语义必须保留。
本计划不修改公开 Object 表示或宿主函数 ABI，不引入并行数值镜像存储。

## CompactValue 与唯一 VM 数组表示（2026-09-13）

VM 寄存器槽已切换为 16 字节 `CompactValue`：数值和布尔内联，资源由私有节点持有；
`BytecodeValue` 同为 16 字节。公开 `Object` 与 `Object::Storage` 仅保留在宿主全局、回调参数、
注册类型转换和最终返回等边界。`named_payload()` 已删除，改为显式区分局部槽与宿主全局对象的
`NamedValueRef`，避免让同一个引用类型同时指向 CompactValue 和 variant。

VM 内部数组唯一表示为 `VmArray{vector<CompactValue>}`。ObjectVector 进入 CompactValue 时立即递归
规范化为 VmArray，离开 VM 时递归导出为 ObjectVector；数组字面量、局部数组、参数数组、range 快照、
单维/多维索引、push/pop/resize/insert/erase/clear/contains 和 size 均已迁移。索引读写使用
`IndexedValueRef` 显式区分 CompactValue 元素与宿主 Object 元素，数组元素不再保存完整 Object 元数据。
宿主全局数组仍是 ObjectVector，这是公开 Object 边界，不是 VM 内部第二种数组表示。

Debug x64 完整回归为 `Passed 77 out of 77 tests.`，包括数组深复制、跨函数值传递、范围快照、
类型约束数组、嵌套数组和错误定位。原样 `cifa/calc-pi.c` 的 Release benchmark 14 次输出一致，
结果长度 502；优化 VM 7 次 execute_ms 为 97.4538、96.5992、96.9960、97.6946、96.8819、
96.8064、99.1574，中位数 96.996ms。

本轮没有做同批 A/B/B/A；上述耗时只能证明
当前实现可运行，不能据此宣称改善。文档最近稳定历史值约 94.908ms，与本轮相差约 2.2%，但跨批次、
不同整机状态，不作为回归结论。后续性能比较应使用同批交错测试。

#### 测量基线

`CIFA_VM_PROFILE` 下，原样 PI 脚本的两次分类计数一致。StoreLocal 快路径 150,917 次，回退 286,641 次。
分类按首个失败条件互斥计数；RHS 类型是另一维度，不能与原因计数相加。

| 回退原因 | 次数 | 占回退比例 |
| --- | ---: | ---: |
| 带类型声明初始化 | 248,476 | 86.7% |
| double 写入 int | 30,081 | 10.5% |
| 复合赋值 | 8,084 | 2.8% |
| 别名及其他 | 0 | 0% |

声明中 int 为 186,172 次，double 为 62,303 次，其他为 1 次；248,476 次重绑全部来自声明。
这些结论只针对该负载，不能推出其他脚本不存在别名或容器赋值热点。
PI 验证要求长度 502、FNV-1a32 为 `1d4b4c2f`，不只检查长度。

#### 优化顺序

1. 局部初始化直接建槽：首步已实现。类型赋值成功后，若当前作用域没有同名对象，且名称未绑定或已绑定当前槽，直接登记槽引用；冲突保留原路径。不改变 RHS 求值时机、类型转换及错误行为。PI 的复制重绑降为 0，直接登记为 248,476 次。
2. double→int 专用写入：已实现有限且在 int64 范围内的快速写入，复用 `Object::toInt64()` 的向零截断。NaN、无穷、越界返回通用路径，快速尝试不修改目标。PI 的该类回退从 30,081 降为 0，StoreLocal fast 为 180,998，fallback 为 256,560。
3. MethodPush 内部分阶段测量：待进行。基于实际容器类型检查、参数准备和写入成本决定下一步。
4. 紧凑值表示与统一局部/临时寄存器：暂缓，需证明前述优化后仍有足够收益。

首步的插桩 bind 时间从约 31.5 ms 降至 10.3 ms，只作为路径成本证据，不当作普通 Release 的提速比例。
验证优先 Debug 回归，覆盖重复声明、遮蔽、循环及兄弟块槽复用、重复执行；之后检查 PI 完整结果。
普通 Release 必须单独测量，保留 warmup 与多次样本；跨批次差值不能直接宣称为严格 A/B 收益。

两步优化后的 Debug 回归为 76/76，包含 AST 与两种 VM 优化模式的局部初始化差分、重复执行，以及数值正负截断、int64 边界、NaN/无穷/越界拒绝且目标不变检查。
新增用例曾因未优化 VM 不支持 `value.size()` 而在翻译阶段失败（`unsupported bytecode node: .`），已移除无关成员调用，保留 string/int 兄弟块复用；该成员调用限制尚未修复。
最终 PI 长度和校验值通过。未插桩优化 VM 中位数为 228.504 ms（六个样本 226.662–229.469 ms），上一批第一步后为 222.564 ms；第二步的整体性能收益尚未证实，不能把回退次数减少等同于端到端提速。

#### 字节码核心成本分析与当前边界

##### 纯值寄存器实现进展

后续槽约束分离：RegisterFile 新增每槽一个字节的 NumericBinding（None/Int/Double），
普通 int/double 值可把绑定保存在槽描述而非 Object 中。带初始化的 StoreLocal 在当前作用域
无绑定冲突时，通过既有 Machine::assign 完成转换后写入纯值槽；非别名 LoadLocal、
RegisterBinary 输入和整数递增不再主动请求 Object&。assign_numeric 可以直接按槽约束转换写入。
read/take/reference 边界会恢复类型约束，clear 会清除约束，保证类型转换和未初始化语义。
已经暴露 Object& 的槽仍保留兼容对象地址，不进行可能使引用悬空的回收。

此步 Debug 76/76，新增绑定提取、复制、消费、转换和兼容还原检查；PI 校验仍为
length=502、fnv1a32=1d4b4c2f。优化 VM 六次中位数199.572ms，范围198.438--200.516ms，
上一批194.181ms，跨批次约慢2.8%，未证明提速。未优化 VM 中位数270.805ms。
仍有边界临时 Object、带约束 copy 的元数据还原、复杂赋值/宿主调用物化和额外存储分支；
本次没有完成全部局部变量去 Object 化，也没有证明上述任一成本是测得回退的主因。

寄存器默认存储已改为 BytecodeValue，仅包含 monostate/int64/double/bool/any payload，
不包含变量名称、声明类型或控制标记。RegisterFile 的数值 binary 路径直接读取 payload，
结果也直接写 payload，不再构造完整 Object。any 仍采用原有值复制语义。

为保留现有 Object& 类型绑定、别名和宿主接口，RegisterFile 使用独立的兼容指针数组：
只有值携带元数据或调用 reference 时才物化 Object，并把 payload 移入其中。
物化后的 Object 是该槽唯一有效值源，指针在窗口扩容后保持稳定；write/clear 保留对象地址，
clear 立即释放 any 负载，不延迟到帧结束。纯值槽 clear 只切换到 monostate。
这不是把所有局部变量元数据都迁到编译期槽描述的最终版本；当前绑定局部仍会物化兼容对象。

Debug 实测 sizeof(Object)=248，sizeof(BytecodeValue)=72，每槽兼容指针为8字节。
这些是当前 MSVC Debug 的类型尺寸，不代表 Release 布局或全部堆分配量；物化槽额外占用一个 Object。
Debug 76/76 通过，新增纯值算术不物化、目标与输入重合、复制/消费、any 最后持有者释放、
兼容引用跨窗口扩容稳定性检查。PI 长度502，FNV1a32=1d4b4c2f。
当前未插桩优化 VM 六次执行中位数194.181ms，范围192.761--194.500ms；
上一批189.741ms，因此本阶段未证明提速，跨批次约慢2.3%，不是严格交错A/B。
下一步应消除局部绑定和运算路径中的 Object& 依赖，将变量约束从值搬到槽描述，
而不是把这个兼容层当作已完成的紧凑运行时。

以下为引入 BytecodeValue 之前的成本分析与其他仍待完成的架构边界。

2026-09-12：运行期块 Scope 已移除两张 unordered_map，改为连续 Binding 描述符。
普通局部及形参指向 RegisterFile 槽；动态创建的局部使用按需分配的稳定地址编号槽。
这只是去掉块哈希表的过渡阶段，不是所有变量访问均已静态寄存器化：Binding 仍按名称线性扫描，宿主 global_variables 仍为哈希表，编译器名称表不在本次删除范围内。
下一阶段需要为动态变量、裸声明的外层复用、成员访问和跨脚本全局访问建立明确的槽/绑定描述符，不能简单让全局值复制到临时寄存器，否则宿主重入及跨脚本修改会失去一致性。

以非别名数值局部的 `total = left + right` 为例，RegisterBinary 局部/常量路径的源码成本如下。
这是路径分析，不是每次运行的分配计数；不同的编译融合和临时操作数路径不能套用同一计数。

| 阶段 | 原有工作 | 原因与当前处理 |
| --- | --- | --- |
| 读取指令 | 读取 RegisterBinarySite、来源及槽编号 | 指令分派、操作数解释和错误定位；仍保留 |
| 准备输入 | 将两个完整 Object 复制到 scratch 槽 | binary 原先只接受同一 RegisterFile 的槽；已改为借用两个 const Object 引用，消除两次复制 |
| 运算 | 检查数值类型、取 int64/double，执行运算 | 动态值语义必需；可进一步研究编译期类型，但不能假定宿主不会改变类型 |
| 产生结果 | 构造数值 Object 并移动写入结果槽 | 目前仍使用携带变量元数据的 Object；纯算术仍需初始化这些字段，这是内部值表示的结构性成本 |
| 写回局部 | assign_numeric 或 Machine::assign | 已绑定内建数值直接更新；声明、注册类型、NoValue 等保留通用语义 |
| 暴露结果 | 需要表达式结果时复制局部值；纯临时结果复制后清空 | 局部仍活跃不能移动；消费完的临时已改为 take+move，无用结果不物化 |
| 退出块 | 清理槽、移除名称绑定、销毁 Scope | 不再构造/销毁两张哈希表，但描述符和动态槽仍有生命周期成本 |

Object 的 payload 是 monostate/int64/double/bool/any，此外有 bound_type、四个 string 元数据及 argument_origin。
基础数值没有用户资源析构，但复用槽仍必须重置“未初始化”状态、类型绑定和来源信息。
clear 已由 `slot = Object()` 改为 payload 置 monostate 并 clear 元数据；基础 payload 无资源释放，any payload 会析构其持有对象。
这保留元数据字符串容量，不在每次清槽销毁字符串存储；最终寄存器文件销毁仍会销毁所有 Object。
因此尚未实现“纯数值槽只含数值、完全没有元数据生命周期”的最终布局。

复制审核结果：

- IncrementLocal 类型判断由 read（复制）改 reference（借用）。
- 后缀递增仅在结果被使用时保存旧 Object。
- DeclareLocal 的独立返回副本在最后使用时移动写入结果。
- RegisterBinary 的消费型临时结果改 take+move；局部/常量输入改借用，不能 move 掉仍活跃的变量。
- 宿主调用和脚本实参已有 take 路径；不能将所有 read/copy 机械替换为 move。
- RegisterSnapshot 可能承担 RHS 副作用前的快照语义，容器值语义也可能要求深复制；必须基于活跃期与求值顺序证明才能删除。

后续架构验收条件：普通块不进行名称登记/查找；变量访问使用预绑定槽；全局与宿主共享同一真实存储；any 的析构时机不延迟，数值槽复用仍报告正确的未初始化错误。
当前只完成运行期块哈希表移除、清槽简化和上述已证明的复制消除，不能宣称这些架构验收条件已全部满足。

## 寄存器原生内建调用（2026-09-14）

原优化调用路径会先把每个寄存器实参导出为 Object 并构造 ObjectVector，再尝试
call_builtin_math。数学函数和基础 to_string 虽然跳过了宿主全局同步，仍已支付 Object
参数构造及结果导入成本。本轮把内建分发前移到 ObjectVector 构造之前，私有接口接收
目标 RegisterSlots/结果槽、参数 RegisterSlots/槽编号数组/数量，直接读取 payload 并写结果槽。
当前覆盖 abs、基础 to_string、全部原有一元和二元数学快路；函数 generation 不匹配、
参数类型不支持或名称不在快路时，仍回退公开 Object(ObjectVector&) ABI。

CallBegin/BindArgument 会把复杂求值的实参移动到独立寄存器窗口，因此原生接口不能只读取
Call 指令的普通输入槽。本轮同时支持 prepared argument window：参数从窗口 0..count-1 读取，
结果直接写入调用者目标槽，成功后按原调用路径恢复窗口。新增 to_string(abs(-12)) 和
pow(abs(-2), sqrt(4)) 回归，优化开关开/关均通过；Debug 总计 77/77。

PI ValueProfile 中 object_exports 从此前约 910 降至 21，约减少 97.7%；数组/map 非空深复制仍为 0。
复杂值探针结果 128126，array_copies=0、map_copies=0，object_exports 从 257 降至 193。
CIFA_VM_PROFILE 中共观察 129 个调用，其中 127 个 to_string 由寄存器快路完成，实际进入宿主
回调仅剩 2 个 println；host_prepare_ms=0.0014，host_invoke_ms=0.1135。两批未插桩 Release
均通过 14/14 输出校验，去除首轮后的优化 VM 中位数约 77.41ms 和 81.15ms；第二批存在整体
环境漂移，因此只能确认没有稳定回退，不能声称本轮另有可靠的执行时间提升。

不建议把公开宿主 ABI 直接改成私有 RegisterSlots* 或裸 Store*：裸指针会暴露窗口偏移、
扩容失效、类型绑定、名称/来源元数据和容器 COW 不变量，并且 AST 后端无法自然提供相同对象。
若继续开放原生宿主函数，宜新增不透明 NativeCallContext，提供按参数索引读取、受控复杂值移动、
直接结果写入及明确的全局读写能力声明；旧 Object(ObjectVector&) 接口继续作为兼容回退。

## 指令协议精简与 Lua 对照结果（2026-09-14）

Lua 5.4 的 opcode 数量并不比 Cifa 少；其执行效率来自固定 32 位格式、编译期表达式描述符、
寄存器/常量/立即数操作数选择，以及让每个 handler 只读取固定字段。Cifa 当前 76 个 opcode
本身不是主要冗余，真正成本仍是通用 Instruction 和部分跨指令协议。随后已删除
`register_inputs` 旁表：验证器直接将连续输入窗口写入 `input_base/input_count`，执行器以
`R(input_base + index)` 解码输入；可变参数调用和多维索引沿用该连续窗口，不创建临时操作数数组。

随后最终执行 `Instruction` 的 destination/input_base/input_count 收窄为 uint32_t，热码从 80B
降为 64B，并在 seal/verify 边界显式拒绝超过 32 位的寄存器编号。Machine 创建时直接物理 resize
256 个寄存器槽，逻辑窗口仍从零开始、超过后按需扩容。共享 RegisterSlots::Storage 记录物理槽总数、当前
活动上界、高水位、扩容次数和清理槽数；`CIFA_VM_PROFILE` 输出这些指标，用于验证 256 槽是否覆盖真实工作负载。
所有容器方法由旧分段协议收敛为 `CheckMethodReceiver + MethodCall`：预检在任何参数求值之前验证 receiver 和方法支持性，
随后 `MethodCall` 直接消费连续参数窗口；单参数 `push_back` 仍可走直接快路径。AST 验证表明 `insert` 也必须在两个参数之前
检查 receiver，因此删除 `MethodInsertBegin/MethodInsertValue`、嵌套参数暂存 vector 和对应寄存器预留。Debug 87/87；
未进行性能基准，不能据指令数或结构尺寸声称提速。
本轮按“先删除空动态协议，再验证格式压缩”的顺序推进，所有候选均先通过 Debug，再用相同 PI
Release probe 交错 A/B；动态指令减少不作为单独保留依据。

最终保留两项。第一，CallEnd 继续用于 verify 的调用帧和诊断帧配对，但 compact 将其与
Enter/Leave/Removed 一同移出最终执行流；执行器不再动态分派 CallEnd。第二，对编译时已知的
native/builtin 调用，verify 完成原有 CallBegin/BindArgument 协议检查和 diagnostic_frames 生成后，
将这两类运行时空指令标记为 Removed；Call 仍直接读取 verifier 生成的参数输入槽。脚本函数调用的
CallBegin/BindArgument 完整保留，因为它们在每个参数求值后立即完成目标窗口移动和类型转换，不能延迟到
Call，否则会改变参数副作用与转换错误的先后顺序。结构测试同时断言最终流不含
Enter/Leave/CallEnd/Removed、native 调用不含 CallBegin/BindArgument、脚本调用仍保留逐参数协议。
最终 Debug x64 完整回归为 89/89。

native 空协议删除按 A/B/B/A/A/B 六批复测，三批基线截尾均值约为 43.27、43.37、44.27ms，
三批候选约为 43.53、43.42、43.13ms；平均约 43.63ms 对 43.36ms，候选约快 0.6%，
所有批次均通过 14/14 输出一致。收益较小但方向稳定，并同时减少最终静态指令，因此保留。

以下候选均撤回：

- SourceRef 从 size_t 收窄到 uint32_t，使 Instruction 从 104 字节降到 96 字节，Debug 89/89；
	A/B/B/A 中候选中位约 44.05/44.88ms，基线约 43.35/42.82ms，平均退化约 3.2%。
	这再次证明不能只缩结构尺寸；当前字段对齐和热字段位置比总字节数更重要。

- 将有参数方法的旧分段初始化合并进首个值消费，Debug 89/89，但累计候选三批截尾均值
	约 45.55/46.12/45.57ms，基线约 43.44/43.37/43.97ms，退化约 5.0%。
	冷初始化代码扩大了热值消费 handler；receiver 的早校验现由独立、轻量的 `CheckMethodReceiver` 在参数求值前完成。
- 删除执行 switch 中 compact 后不可达的 Enter/Leave/Removed case，Debug 89/89，但 A/B/B/A
	候选截尾均值约 45.06/45.10ms，基线约 43.26/43.05ms，退化约 4.5%。这些 case 虽不可达，
	仍影响巨大 switch 的代码布局，因此恢复为 continue 占位。

PrepareStore 也不能按名称机械删除：普通带类型目标需要在 RHS 前创建并绑定，成员目标需要先解析接收者，
索引目标需要先求值并冻结索引，三者都承担左值只求值一次及错误顺序语义。后续若继续精简，应先引入
编译期 lvalue 描述符或直接目标槽，而不是把 PrepareStore 工作延迟到 Store。

剩余指令集改造按风险分为三类：

1. 已完成且应冻结的热路径：连续 `A/B/C` 输入窗口、脚本 `Call`、通用 `MethodCall`、本地数值和数组快路，以及 256 槽初始物理池。
2. 必须按 AST 语义逐项证明的 Cifa 协议：`Prepare*Store` 的左值冻结；`Range*`、`Switch*`、`Scope*` 的动态作用域、快照和诊断边界，以及脚本调用的逐参数转换。它们不能仅因 Lua 没有同名 opcode 而删除。方法 receiver 的早校验已统一为轻量 `CheckMethodReceiver`，不再存在 `MethodInsert*` 专用协议。
3. 真正仍可推进的结构改造：将 `Prepare*Store` 编译为显式 lvalue 描述符或固定目标寄存器，逐步减少通用 prepared-store 记录；为常量、短小立即数和固定字段访问增加更紧凑编码；在 profile 证明超过 256 槽后再调整初始池大小。Lua 的 32 位 packed instruction 可作为长期格式目标，但必须在 Cifa 元数据和诊断字段冷分离后实施，不能通过删除类型转换、COW 或错误顺序换取表面一致。

### Store 与诊断标记的重构决策（2026-09-14）

Lua 5.4 没有 Cifa 这种通用 `PrepareStore` + `Store` 对。`luaK_storevar()` 先在编译器的 `expdesc`
中保留左值类别，然后直接发射：局部变量将 RHS 直接写到局部寄存器，upvalue 用 `OP_SETUPVAL`，索引按
键类别发射 `OP_SETTABLE`、`OP_SETI` 或 `OP_SETFIELD`。这并非取消“先确定左值”的工作，而是把该工作
放到编译期描述符，最终指令显式携带目标槽、接收者和键。

Cifa 当前的 `PrepareStore` 有真实语义：它在 RHS 前创建声明绑定和类型约束、解析成员，或求值并冻结索引；
`Store` 才读取 RHS、处理 `=`/复合赋值并写回。这个拆分保证 `a[f()] += g()` 中 `f()` 只执行一次，且保持
左值错误、RHS 错误和类型转换的原有先后顺序。因此不能直接删除或把 `PrepareStore` 移到 `Store` 之后。
问题不在语义，而在一个通用运行时协议同时编码了 local/global/member/index 和声明/复合写入的所有组合。

迁移顺序如下：

1. 引入仅编译期存在的 `StoreDescriptor`，显式记录 Local、Global、Member 或 Indexed 左值，接收者/索引
	的已求值槽、声明和类型绑定信息，以及写入操作。
2. 先让 local `=`、局部声明和局部复合写入直接发射到目标槽；已经落地的 `ConstantLocal`、
	`NumericBinaryLocal` 是这一方向的第一批，不再扩展 `PrepareStore`。
3. 将 global/member/index 分别迁为直接 store 指令或固定的“解析左值结果槽 + 写入”协议；每一类迁移必须
	覆盖左到右求值、一次性索引求值、alias、COW 容器、类型转换及精确诊断顺序后，才删除其旧路径。
4. 当所有发射点都不再产生通用对时，删除 `PrepareStore` 与 `Store`，而不是保留名称不同但语义相同的新空标记。

2026-09-14 第一阶段已落地：所有赋值、自增和自减目标先通过 compiler-only `StoreDescriptor` 归一化为
Local/Named/Member/Indexed。描述符唯一创建 VariableSite、IndexSite 或 MemberSite，并携带对应编号；发射端
不再从 AST 重建这些元数据。Local 目标统一由描述符提供槽号，继续选择 `ConstantLocal`、`NumericBinaryLocal`
或 `StoreLocal`。非局部写入现明确发射 `PrepareNamedStore/StoreNamed`、
`PrepareMemberStore/StoreMember`、`PrepareIndexedStore/StoreIndexed`；自增/自减同样区分
`IncrementNamed/IncrementMember/IncrementIndexed`。准备状态随脚本调用帧保存，索引键仍在 RHS 前只计算一次；
写入端要求消费同类别的准备描述符，不能再通过 operand/member/variable 三字段推断目标类别。这是 Lua
`expdesc` 和 `SETTABUP/SETFIELD/SETI/SETTABLE` 的类别显式化方向，不做 benchmark，也不据此声称性能变化。

Lua `GETI` 的常量整数索引候选未保留：Cifa typed array 的元素绑定与动态容器初始化仍依赖 `Index` 的完整
通用协议，初版 `IndexInteger` 在 `typed_array_and_struct_test` 破坏行为。后续只有在 receiver/type 描述符可
显式携带元素约束后，才能安全跳过常量键临时寄存器；不要以“数组读取看似等价”为由绕过通用路径。

固定成员读取 opcode 由 `Member` 改名为 `GetField`，对应 Lua `GETFIELD`：两个名称池操作数分别是
全局 receiver 与字段名。它仍只覆盖静态 `a.b` / `a::b` 读取；动态键与 typed array 仍由 `Index` 处理。
同一字段左值族由 `PrepareFieldStore` + `SetField` / `IncrementField` 表达；prepare 阶段仍必须保留，
以冻结左值求值并跨 RHS 中的嵌套脚本调用保存它。

其余通用容器和全局写入名称也按语义收敛：`NewArray`、`GetIndex` / `GetIndexLocal`、
`PrepareGlobalStore` / `SetGlobal` / `IncrementGlobal` 与 `PrepareIndexStore` / `SetIndex` /
`IncrementIndex`。这只是对既有合同的命名整理，typed array、map 和动态索引仍共享原有执行协议。

2026-09-14 第二阶段已落地：`describe_leaf_expression()` 成为数值常量和已绑定局部的唯一
`ExpressionDescriptor` 入口，数值二元树、普通叶子发射和直接 return 共享 Constant/Local 分类；未分类节点
仍走原有通用 emitter。`if` 与三元表达式的 Branch/Jump 改为 compiler-only patch-list helper 统一生成和回填。
最终字节码协议、调用窗口和 VM handler 均未变；Debug x64 完整回归 89/89，未 benchmark。

Lua 也没有普通表达式的 `Enter`/`Leave`。它把 PC 到源码行的映射、局部变量生命周期和调试 hook 放在
prototype/调用帧元数据中；只有 `OP_CLOSE`/`OP_TBC` 这类影响 upvalue 或 to-be-closed 资源的指令有运行时
语义。Cifa 的 `Enter`/`Leave` 已仅用于构建诊断帧并在 `compact()` 中移除，最终执行流不执行它们。因此它们
不是 VM 应保留的标志指令，后续应继续收敛为编译期/冷诊断元数据；不能与有真实资源释放语义的 `ScopeLeave`、
`Unwind` 混为一谈。

该迁移现已完成：正常编译发射路径不再生成 `Enter`/`Leave`。编译器将表达式、函数调用和 `return` 的
诊断范围记录为仅构建期存在的范围事件；`seal()` 将事件展开为每个最终 PC 的 `diagnostic_frames` 冷表，
`compact()` 继续与 code 和 diagnostics 一起重映射。函数调用帧以实际保存的 `Call` PC（兼容旧 `CallBegin`）
识别，因此保留脚本函数名、嵌套表达式位置及 `NoValue`/数值错误的完整调用栈。保留 enum、verify 和执行器中的
旧 opcode 兼容分支，待非法字节码测试与格式协议清理切片统一删除。Debug x64 完整回归 89/89；未做性能基准。

上述结论只适用于此前的独立 lower 执行数组实验；后续已由如下单流冷热分离替代。

## 执行码与诊断码冷热分离（2026-09-14）

最终执行流现在仍只有一份 `Instructions::code`，但构建阶段使用含 `SourceRef` 的
`BuildInstruction`；`seal()` 将其投影为不含源码字段的最终 `Instruction`，并将
`source/condition_source/target_source` 放入与 PC 严格对齐的冷 `InstructionDiagnostic` 表。
`compact()` 同步重映射 code、diagnostics 和 diagnostic_frames，verify 继续在最终 code 上运行，
因此没有第二份执行数组，也没有在执行时反查或转换另一份操作数流。

MSVC x64 静态断言确认最终 `Instruction` 为 80B，替代原来携带三个 SourceRef 的 104B 执行对象。
诊断表只在分支条件、显式目标位置、赋值慢路、类型/未初始化错误和 Object runtime reporter 中按 PC 读取；
主循环取指、常量、跳转、scope 和纯数值快路不再预取 diagnostic，也不再预先解析 SourceLocation。赋值慢路
仍可用 site.assignment_source 覆盖默认 PC 来源，保持原有目标位置诊断。

Debug x64 完整回归为 89/89；现有错误正文、源码行和 `^` 对齐测试继续通过。Release benchmark 也通过
14/14 输出一致性校验（502 字符）。本轮 Release 样本为 43.45--47.76ms，但没有冻结重构前二进制进行
交错 A/B，因此这些样本只证明无功能回退，不能作为本轮性能提升结论。后续若评估收益，必须冻结基线并按
A/B/B/A/A/B 交错执行。

## 2026-09-15 单批 Release PI 账本

以下均使用 benchmark 程序的 optimized-bytecode `execute_ms`，每批 7 个样本，且均为
`PASS: all 14 outputs identical, characters=502`。后文只列出有助于结论的中位数和汇总结果。

### 最早同进程优化开关对照（2026-09-12）

早期 benchmark 程序在同一进程内分别测量未优化与优化的字节码执行。该批记录的中位数为：

| 配置 | optimized-bytecode execute 中位数 |
| --- | ---: |
| 未优化 | `355.710 ms` |
| 优化 | `313.109 ms` |

优化包含当时的常量折叠、数值内建直达和局部一维数组直接路径；同进程差异为约 `$11.98\%$`。

### 数值表达式临时槽按活跃深度复用

- 样本：`40.05, 40.33, 39.91, 39.68, 41.35, 39.62, 41.73 ms`
- 中位数：`40.05 ms`
- 对照范围：此前相邻批次约 `39.95--40.16 ms`。
- 结论：仅确认临时槽容量下降与输出一致；未测得 PI 加速。

### 显式 double 目标槽直写 A/B/A

| 批次 | 配置 | 7 个 execute_ms 样本 | 中位数 | 输出 |
| --- | --- | --- | ---: | --- |
| A1 | 开启 `NumericBinaryLocal` double 目标直写 | `39.2162, 39.2155, 39.0325, 39.1978, 39.3397, 38.9473, 39.2692` | `39.216 ms` | 14/14 一致 |
| B | 关闭该直写路径 | `41.4420, 42.8703, 41.4606, 42.2239, 41.3297, 41.1955, 41.2267` | `41.442 ms` | 14/14 一致 |
| A2 | 恢复开启 | `39.6604, 39.2968, 39.3303, 39.0857, 39.1223, 42.9566, 39.7072` | `39.330 ms` | 14/14 一致 |

结论：A/B/A 显示约 `$5.3\%$` 收益；保留实现。

### 丢弃结果的局部赋值 copy-to-move A/B/A

| 批次 | 配置 | 7 个 execute_ms 样本 | 中位数 | 输出 |
| --- | --- | --- | ---: | --- |
| A1 | 无类型、非 alias 的 `StoreLocal` 丢弃结果时 move RHS | `40.1639, 39.7089, 39.2367, 39.1485, 38.8574, 39.4809, 39.0295` | `39.237 ms` | 14/14 一致 |
| B | 同路径保持 copy | `39.5396, 39.3413, 38.9224, 39.9151, 38.8046, 39.1449, 38.9276` | `39.145 ms` | 14/14 一致 |
| A2 | 恢复 move | `40.1405, 40.4522, 39.0074, 40.0387, 40.7636, 39.7383, 47.4263` | `40.039 ms` | 14/14 一致 |

结论：两次 A 的波动超过 A/B 差异，不能宣称 PI 加速；保留为受限所有权转移。该路径只覆盖无显式类型、非 alias、普通局部赋值且结果被丢弃的已物化 RHS，不能机械扩展到类型转换、复合赋值、全局、成员或索引存储。



## 2026-09-15：标准 PMR 与作用域临时存储

本轮将 VM 内部容器迁移到标准 PMR，通过构造参数显式传递资源；生命周期明确的临时数据使用栈缓冲区。赋值路径
复用诊断名称 ID，早期的自定义分配器和缓冲池实现已删除。资源接口及宿主边界见 [cifabytecode.md](cifabytecode.md)。

在 Ryzen 7 9800X3D、Windows、MSVC x64 Release 上，标准内存池版本的 PI 执行时间为 `41.0412 ms`，20,000
次调用为 `22.5789 ms`；上一版分别为 `43.5414 ms` 和 `24.4955 ms`，约快 5.7% 和 7.8%。这是整次迁移的结果，
不能单独归因于栈缓冲区。预热后的资源计数也显示，标准池可以满足后续请求而不再向上游申请内存。

Release 和 Debug 的回归测试、池分配测试及 allocator 测试全部通过；库本身不修改全局默认 PMR 资源。

## 2026-09-15：VM 字符串与安全的 string_view

脚本字符串已从 `std::any` 移入 variant 的 `VmString`，由 `std::pmr::string` 和资源所有权组成；VM map 的键同时改为 PMR 字符串。复制显式使用原资源，移动转移所有权，宿主接口仍接收/返回普通 `std::string`。拼接、格式化和 native 返回直接构造 PMR 字符串，格式化写入 PMR 输出缓冲区。

`string_view` 只用于当前操作中的查找和解析：map/作用域查找、诊断源文本、格式串及其子串。解析完成前不替换源寄存器。嵌套执行前保留脚本文本副本，避免重入使视图失效。公开返回值不借用 VM 字符串。未知宿主载荷仍保留 `std::any` 兼容路径；独立 `PmrAny` 暂未接入此路径。

Release 和 Debug 的四个 CTest 目标全部通过。新增用例覆盖 512 字节字符串、长 map 键、数组/map 的 COW 隔离、格式结果覆盖原格式变量、嵌套 native 回调、类型推导及 VM 销毁后的结果读取。原有 NoValue 错误语义保持不变。

### 性能结果

在直接分配和标准池两种模式下做同批 A/B/B/A/A/B 测量，每种模式和负载各记录三批、每批 15 次执行。三批中位数的中位数如下：

| 模式 | 负载 | 修改前（ms） | 修改后（ms） |
| --- | --- | ---: | ---: |
| 直接分配 | PI | 42.5363 | 42.0766 |
| 直接分配 | 20,000 次调用 | 28.5994 | 28.2734 |
| 标准池 | PI | 40.7282 | 40.6594 |
| 标准池 | 20,000 次调用 | 22.4901 | 22.3494 |

变化约为 0.2%–1.1%，不足以宣称有明确的整体提速。新增 `strings` 负载覆盖长字符串返回、拼接、格式化和长度累加，结果为 `1388890`，并与 AST 后端一致。当前版本的分配模式对比为直接分配 `17.9966 ms`、标准池 `14.4046 ms`；这是模式之间的差异，不是字符串迁移的前后收益证据。


## 2026-09-15：clang-cl inline budget 实验与 dispatcher handler 拆分（独立测机）

### 2026-09-22：当前源码复测

使用 VS2026 自带的 Clang 22.1.3、`/O2 /DNDEBUG` 和同一份当前源码，交错运行默认配置与
`-mllvm -inline-threshold=10000 -mllvm -inlinehint-threshold=10000`。每种配置各收集 21 个
`bytecode_benchmark` 优化 VM PI 样本，全部 `PASS: all 14 outputs identical, characters=502`：

| 配置 | 中位数 execute_ms | 平均 execute_ms |
| --- | ---: | ---: |
| clang-cl 默认 inline budget | 25.27 | 25.23 |
| clang-cl 高 inline threshold | 23.51 | 23.47 |

高阈值中位数减少 `1.7571ms`，约快 `6.95%`。该结论只适用于本机当前源码的 PI 热路径；它说明当前
dispatcher 仍受 Clang 默认 inline budget 约束，不代表小型 increment 负载或 MSVC 构建也会同样受益。

同日还重新测试了 `IntIncrementLocal` 与 `IntForNext` 专用 opcode 候选。此前 MSVC `/O2` 的回退不能
代替高 inline Clang 结论，因此本次候选与 `0dc6223` 基线均使用上述高 threshold、交错各 21 个 PI 样本。
两侧均通过 14/14 输出一致；基线中位数 `22.45ms`，候选 `22.42ms`，仅 `0.13%` 差异，且三个交错周期
方向不一致。该候选没有可归因收益，已撤回；不能据此将高 inline 的 6.95% 收益归因于新增整数 opcode。

这组实验是在另一台 CPU 上完成的。基线提交 `524f810` 已经包含 `2ab6301` 的 allocator 和 scoped guard 优化，
所以这里只比较后续的 handler 结构改动，不能把结果和上面的主表跨机器相减。`84176aa` 只改了测试源码，
没有改变 VM 二进制，因此不单独列入测量。

### clang-cl 三组编译配置（拆分前）

为了确认问题是否来自编译器的 inline budget，我们用同一份源码测试三种配置：MSVC、默认的 clang-cl，
以及把 inline threshold 调高的 clang-cl（`-mllvm -inline-threshold=10000 -mllvm -inlinehint-threshold=10000`）。
三种配置交错运行，每种配置做 3 轮、每轮 15 次采样：

| 负载 | MSVC | clang-cl 默认 | clang-cl（高 inline threshold） |
| --- | ---: | ---: | ---: |
| PI | 39.30 | 29.50 | 22.20 |
| 20,000 次调用 | 22.81 | 17.64 | 15.50 |
| increment | 5.96 | 6.60 | 6.62 |

在 clang-cl 的两个配置之间，只提高 inline threshold 就让 PI 快了 `24.7%`，这表明巨大的 `switch` 已经用完了
inline budget；MSVC 没有对应的开关。在 `increment` 负载上，MSVC 反而快约 11%，调高 clang-cl 的 threshold
也没有帮助。这说明小而常用的 handler 并不受 inline budget 影响，真正的问题在巨大的 handler 本身。Lua 的
参考结果来自本机的 MSVC 构建，并且同样使用 `switch` dispatcher，所以这个比较是可比的。

### 拆分 handler（两批提交）

我们新增了 `CifaBytecode::InterpState`，把执行循环需要的可变状态集中到一个状态对象中；原来写在循环里的
lambda 辅助函数改成了成员函数，头文件只增加一行前置声明。`NumericBinary` 和 `NumericBinaryLocal` 的
`register_binary` fallback 路径也改成直接调用。之后分两批把 22 个大型或低频的 handler 移到
`CIFA_NOINLINE` 成员函数，让 dispatcher 主循环更小。根据 A/B 结果，`IncrementLocal`、`ArrayPushGlobal` 和
`StoreLocal` 仍保留在 inline 路径中。每个提交都在同一次测试会话中重建并交错运行，每轮 15 次，共 3 轮：

| 提交 | PI | calls | increment | incrementf |
| --- | ---: | ---: | ---: | ---: |
| `524f810` 基线 | 38.40 | 21.99 | 5.96 | 185.7 |
| `033101d` 六个大型 handler | 33.48 | 20.97 | 5.79 | 182.3 |
| `0159b4a` 十六个低频 handler | 31.83 | 20.90 | 5.76 | 176.7 |

相对基线，这次会话中的累计变化是：PI 约 `-17.1%`、calls 约 `-5.0%`、increment 约 `-3.4%`、incrementf
约 `-4.9%`。我们也试过把 `StoreLocal` 的低频路径拆出去，但结果在不同会话中方向相反：一次快 `3.4%`，
另一次慢 `3.3%`。因此这项改动不够稳定，最终撤回。所有批次的 Debug 回归测试都通过了（90/90）。

### clang-cl 复测（拆分后）

| 负载 | MSVC | clang-cl 默认 | clang-cl（高 inline threshold） |
| --- | ---: | ---: | ---: |
| PI | 32.83 | 27.94 | 21.88 |
| 20,000 次调用 | 20.64 | 17.91 | 15.60 |
| increment | 5.76 | 6.64 | 6.61 |

拆分后，MSVC 与默认 clang-cl 的 PI 差距从约 `25%` 缩小到约 `15%`；与高 inline threshold 的 clang-cl 的
差距则从约 `43%` 缩小到约 `33%`。默认 clang-cl 自己也变快了，说明缩小 handler 体积对两种编译器都有帮助。
即使把 threshold 调高，PI 仍比默认配置快约 `22%`，说明剩下的热点部分（`LoadLocal`/`Peek`、
`NumericCompareBranch`、`Branch` handlers、`IncrementLocal` 等）仍受到默认 inline budget 的限制。后续应
一次只拆一个 handler，把 fast path 留在 inline 路径，把低频 tail path 设为 `noinline`，并用 A/B 测试逐项确认。
`increment` 上 MSVC 的优势来自生成的循环代码，与 dispatcher 的结构无关。

## 2026-09-20（一）：复合赋值修复与整数/浮点 fast path

### 问题

`total += i` 比 `total = total + i` 慢约 83%（33.5ms 对 18.3ms，100 万次迭代，编译与执行分开计时）。
用新增的 `CifaBytecode::dump_instruction_listing()` 打印两者的指令清单后发现：真正做加法的指令完全一样，
区别是 `+=` 版本的循环体每圈多跑三条纯管理指令（ScopeEnter、LoopMark、ScopeLeave）。

原因：编译器的作用域分析对 `=` 有专门处理（能证明"纯数值赋值不需要作用域"），
但 `+=` 没有对等的分支，只能按最坏情况处理，于是强制整个循环体保留作用域。

### 修改

- 作用域分析新增 `+=`/`-=` 等复合赋值分支：当目标和右值都是已知数值变量时，
  按 `X = X op Y` 对待，不再强制作用域。
- `NumericBinaryLocal` 在主 switch 里加了 fast path：当两个操作数都是本地槽位、
  目标槽位的数值类型已确定、且结果不需要写回寄存器时，直接在本地槽位上完成运算，
  不再调用通用函数。整数覆盖加/减/乘/位运算（溢出按回绕处理），浮点覆盖加减乘除；
  除法、取模、移位（有除零/越界检查）仍走原路径。
- 循环变量递增的 fast path（`NumericForNext`/`IncrementLocal`）从只认 int64 扩展到
  也认 double，直接原位加 1.0。

### 结果

独立计时程序（`build/probe/bench_loop.cpp`，15 次取最优）：

| 脚本（100 万次迭代） | 修复前 ms | 修复后 ms |
| --- | ---: | ---: |
| `total += i` | 33.55 | 10.49 |
| `total = total + i` | 18.27 | 10.43 |
| `value++` | 8.35 | 7.68 |
| 空循环体 | 8.48 | 8.02 |

标准 `cifa_benchmark --vm-only`（10 次中位数）：

| 负载 | 修复前 ms | 修复后 ms |
| --- | ---: | ---: |
| increment | 8.49 | 8.14 |
| incrementf | 182.95 | 9.49 |
| calls | — | 23.34 |
| strings | — | 15.64 |
| pi | — | 36.33 |

incrementf 提升 19 倍：之前 double 循环变量的递增走通用路径，每圈都要查作用域、
做类型标记和一次通用加法；现在和整数循环一样是两条内联指令。
（increment 无回归：交替重建对比 8.49 对 8.14。）

## 2026-09-20（二）：循环体变量声明提升（hoisting）

### 问题

PI 的指令统计显示，23.2%（64.8 万条）的动态指令是 ScopeEnter/ScopeLeave/LoopMark，
全部是纯管理开销。来源：PI 的循环体里声明了局部变量（如 `int val_a = 0;`），
而当时的规则是"块里只要有声明就要建作用域"。Lua 对同样的代码是把声明分配成寄存器
槽位，运行时没有任何作用域指令。

### 修改

编译器现在把这类声明"提升"到函数级别的槽位，循环体不再建作用域。核心改动是
作用域分析给每个块记一个更精确的结论："这个块是否真的需要在运行时建作用域"。
以下情况仍然保留（它们依赖作用域的运行时行为）：

- 不带初始值的声明（`int x;`）；
- 声明会遮蔽外层同名变量（内外两个变量需要各自独立的槽位）；
- 标签和 goto；
- 向编译期无法确定的名字赋值（可能要在运行时建立绑定）。

配合两点：

- 维护一份"编译期已确定是局部变量"的名单。之前任何函数调用都会让分析器忘记
  所有已知信息（保守处理），导致循环里的普通赋值（如 `rem = cur % divisor`）也被
  当成"未知名字"而强制作用域。名单不会被忘记操作清空，这类赋值现在可以提升。
- 提升出去的槽位进入按函数隔离的复用表：前后两个兄弟块声明同名变量时复用同一个
  槽位，保证运行时的名字解析和编译期分配一致。

调试过程中顺带发现并修复了三个原有的寄存器存储问题（以前一直存在，只是作用域
清理恰好把痕迹擦掉了，提升后暴露出来）：

1. 函数调用创建局部窗口时不清空槽位——残留的数值类型标记会让"字符串存进 int
   变量"绕过类型检查、不报错。现在进入时清空。
2. `int`/`double` 类型标记的推断不认识数组值，会把数组误标成整数。现在只对
   认识的值类型做推断。
3. 变量赋值时直接沿用来源寄存器的类型标记（可能过期）。现在赋值完成后按实际
   存入的值重新确定。

### 结果（cifa_benchmark --vm-only，10 次中位数）

| 负载 | 提升前 ms | 提升后 ms |
| --- | ---: | ---: |
| pi | 36.33 | 30.78 |
| calls | 23.34 | 21.86 |
| strings | 15.64 | 14.95 |
| increment | 8.14 | 7.75 |
| incrementf | 9.49 | 9.67（噪声范围内） |

PI 执行的指令总数从 279 万降到 216 万（-22.8%），其中作用域类指令从 64.8 万条
降到 1 万条（只剩函数入口一次性的那部分）。

正确性验证：全部 90 个单元测试（Debug 和 Release 都跑过）；另外写了 19 个针对性
测试用例——同名变量出现在前后两个循环里、内层变量遮蔽外层同名变量、循环内声明
数组或字符串、声明后接 break/continue、goto 跨块、while/do-while 循环——
字节码和 AST 两种解释器的结果全部一致。

下一步（对应 2026-09-14 的分析）：剩余差距主要在每条指令的固定成本——104 字节的
Instruction 结构解码、操作数表和元数据表——这是 Lua 式 32 位指令重构要解决的问题。

### 优化前后对照（2026-09-20，a4f2a93 对当前，7 轮轮转取中位数）

| 负载 | 优化前 ms | 优化后 ms |
| --- | ---: | ---: |
| pi | 36.06 | 29.95 |
| calls | 22.61 | 21.77 |
| increment | 8.47 | 7.74 |
| incrementf | 184.21 | 9.75 |
| strings | 15.31 | 14.95 |

优化前二进制单独构建后与当前构建交替运行（每负载 7 轮、次序轮转，表中为
中位数）。pi -17%，incrementf 18.9 倍，increment -9%，calls -4%，strings -2%。
收益集中在循环结构上；calls/strings 的瓶颈在调用与字符串处理本身。
MSVC 下 pi 对 Lua（同机 9.34ms）为 3.2 倍。

### noinline 策略（2026-09-20）

22 个 handler 的强制 noinline 改为默认交给编译器决定（宏可覆盖）。MSVC 七轮
交替对照：放开后 increment -6%、incrementf -4%、calls -2%，pi/strings 持平。
2026-09-15 的"拆分后保持 noinline"结论基于当时的分派器结构，已由本轮取代。
