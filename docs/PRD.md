# bmcli 项目 PRD（总结版，修正版 V0.4）

> 更正：**bmcli = BMC CLI（Baseboard Management Controller 命令行工具）**，面向服务器带外管理（OOB）场景；不等同于 BareMetal 平台客户端。以下为按 **BMC 管理** 语义修订后的总结性 PRD。文中 **[待确认]** 需结合你们现有 bmcli 代码与实际支持协议（IPMI/Redfish/厂商私有）校准。

最后更新：2026-03-12

## 0. 文档状态与原则

- 本 PRD 面向「可落地实现」：命令、输出、错误码、配置格式尽量具体；确实不确定的地方用 **[待确认]** 标记，并在文末集中成清单。
- 默认协议优先级：Redfish（HTTPS/JSON）优先，IPMI（lanplus）作为补充/兜底 **[待确认]**。
- 默认行为偏安全：TLS 校验默认开启；破坏性操作默认二次确认；日志默认脱敏。

## 0.1 术语与定义

- `BMC`：服务器带外管理控制器（Baseboard Management Controller）。
- `OOB`：带外管理（Out-of-Band），与业务网/操作系统无关的管理通道。
- `Redfish`：基于 HTTPS/JSON 的管理标准协议（DMTF）。
- `IPMI`：Intelligent Platform Management Interface；常见实现为 `ipmitool` lanplus。
- `SEL`：System Event Log，BMC 侧事件日志。
- `SOL`：Serial over LAN，串口带外控制台。
- `KVM`：远程键鼠与视频控制台（多为厂商 OEM 能力）。
- `Target`：一个可连接的 BMC 实例（host[:port] + 协议 + 凭据）。
- `Profile`：一组可复用配置（默认协议、TLS 策略、凭据来源、超时/并发等）。

---

## 1. 产品概述

### 1.1 产品名称
**bmcli（BMC CLI）**

### 1.2 一句话定位
用于对服务器 **BMC 带外管理**进行统一操作的命令行工具，覆盖电源控制、传感器/日志、远程控制台、固件与配置管理，并可脚本化集成到运维与生产流程。

### 1.3 背景与问题
- 数据中心现场/远程故障处理依赖 Web BMC 或厂商工具，效率低、难批量、难审计。
- 多厂商 BMC（不同 UI/接口/能力）导致工具碎片化。
- 需要在自动化系统中统一执行：上电/下电、获取硬件健康、拉取 SEL、开 SOL/KVM、固件升级等。

### 1.4 目标（Goals）
- 提供统一、稳定、可脚本化的 BMC 管理命令集合。
- 支持多目标批量操作与并发控制。
- 兼容主流协议：**Redfish 优先**，IPMI 作为补充/兜底 **[待确认]**。
- 输出可机器解析（JSON）+ 易读（table），并具备可审计性（日志、请求 ID）。

### 1.5 非目标（Non-goals）
- 不替代厂商完整 GUI（仅覆盖常用/自动化高频能力）。
- 不实现复杂资产系统/CMDB（可对接但不承担其职责）。
- 不在 CLI 内实现固件仓库/镜像分发平台（可调用外部 URL/文件）。

---

## 2. 用户与使用场景

### 2.1 目标用户
- IDC 运维/SRE：故障处置、批量巡检、带外操作
- 硬件/交付工程师：上架初始化、BIOS/BMC 配置固化、固件升级
- 自动化平台/CI：健康检查、出厂测试、回归测试、批量配置

### 2.2 核心场景
1. **电源与重启控制**：power on/off/cycle/reset，支持批量与超时控制。
2. **健康巡检**：读取传感器（温度/风扇/电源/电压）、系统健康汇总。
3. **日志取证**：拉取/清空 SEL（System Event Log）与事件详情。
4. **远程控制台**：SOL（Serial over LAN）连接、KVM/虚拟介质挂载 **[待确认]**。
5. **固件与配置管理**：BMC/BIOS 固件版本查询与升级；导出/导入配置；用户/网络配置。
6. **批量初始化**：设置 BMC IP/网关/VLAN、创建用户、设置安全策略等。

---

## 3. 范围与版本规划（建议）

### 3.1 MVP（V1.0 必须有）
- 目标管理：支持指定单个 BMC 或从文件/管道读取多目标
- 鉴权：用户名/密码、token/session **[待确认]**
- 电源控制：status/on/off/cycle/reset
- 传感器：list/get（含健康状态）
- SEL：list/get/clear
- 输出：`-o table|json`，稳定字段
- 调试：`--debug`（脱敏），明确 exit code

### 3.2 V1.1（增强）
- SOL：connect/disconnect（可选 keepalive、日志落盘）**[待确认]**
- 网络配置：BMC IP、DHCP/static、DNS、NTP、时区
- 用户管理：list/add/del/role/password rotate
- 并发与重试：`--concurrency`、`--retry`、`--timeout`

### 3.3 V2.0（进阶）
- Redfish 虚拟介质：mount/unmount（ISO/IMG）
- 固件升级编排：上传、触发升级、任务跟踪、回滚策略（取决于硬件）
- 配置基线：`apply -f baseline.yaml`（声明式）
- 插件/适配器机制：按厂商扩展能力（Dell/iDRAC、HPE/iLO、Lenovo XCC 等）**[待确认]**

---

## 4. 功能需求（按模块）

### 4.1 目标与连接（Targets）
**需求**
- 支持以下方式指定目标：
  - `--host <ip>` 单目标
  - `--targets <file>` 多目标（每行 host/port/user/pass 或引用 profile）
  - 从 stdin 接收（便于管道）
- 多目标的输入格式需要固定并可被校验，建议支持两种互补格式：
  - 简单行格式（便于快速批量）：`host[:port] [user] [pass] [protocol] [profile]`
  - 结构化格式（便于长期维护）：YAML/JSON `targets.yaml`（见 4.1.1）
- 支持 HTTPS/证书校验开关（生产默认校验；允许 `--insecure`）
- 支持代理与超时：`--connect-timeout`、`--timeout`
- 支持目标级覆写：命令行参数应可覆盖 profile 与 targets 文件中的默认值（例如 `--timeout`、`--insecure`）。

**验收**
- 批量目标中单个失败不影响整体（可配置 fail-fast）
- 输出包含每个目标的结果与失败原因

#### 4.1.1 Targets 结构化格式（建议）

文件名建议：`targets.yaml`（或 `targets.json`）。字段建议：

- `targets[]`：
  - `name`：目标别名（用于输出与审计）
  - `host`：IP 或域名
  - `port`：默认 443（Redfish）或 623（IPMI）**[待确认]**
  - `protocol`：`redfish|ipmi|auto` **[待确认]**
  - `username`：可选（建议更优先引用凭据 ID）
  - `password`：可选（不建议明文存放，建议仅用于临时/测试）
  - `credential_ref`：引用本机安全存储中的凭据 ID **[待确认]**
  - `profile`：引用 profile 名称

### 4.2 鉴权与配置（Auth/Config）
**需求**
- profile：`dev/test/prod` 或按机房/厂商分组
- 凭据存储安全：本地配置权限校验（600）、日志脱敏
- 支持环境变量覆盖（自动化友好）

**配置文件建议**
- 配置路径优先级（高 -> 低）：CLI flags -> env -> profile -> config file -> defaults
- 配置文件位置（建议）：`~/.config/bmcli/config.yaml`（Linux/macOS）**[待确认]**
- env 约定（建议）：`BMCLI_HOST`、`BMCLI_USER`、`BMCLI_PASS`、`BMCLI_PROTOCOL`、`BMCLI_INSECURE`、`BMCLI_TIMEOUT`

### 4.3 电源管理（Power）
**命令建议**
- `bm power status|on|off|cycle|reset`
- `--yes` 跳过确认（对 off/cycle/reset 默认二次确认）

**验收**
- 幂等：重复 on/off 有明确提示与正确返回码
- 失败时给出 BMC 返回码/消息（例如 Redfish ExtendedInfo）

### 4.4 监控与健康（Sensors/Health）
**能力**
- `bm sensor list/get`
- `bm health summary`：汇总硬件健康（CPU/内存/风扇/电源/温度/存储控制器等，按可得数据）**[待确认]**

### 4.5 日志（SEL / Event Log）
**能力**
- `bm sel list|get|clear`
- 支持导出：`--out sel.json` 或 `--out sel.csv` **[待确认]**

### 4.6 远程控制台（SOL/KVM）（可选）
- `bm sol connect`（交互式）
- `bm sol log --duration 10m --out sol.log` **[待确认]**
- KVM/虚拟介质根据 Redfish OEM 能力实现 **[待确认]**

### 4.7 固件与配置（Firmware/Config）（可选）
- `bm firmware list`（BMC/BIOS/FPGA 等版本）
- `bm firmware update --file xxx` 或 `--url xxx` + `bm task wait`（若异步）
- `bm config export/import`（基线固化）

---

## 5. 命令设计（建议草案）

> 二进制名以 `bm`/`bmcli` 其一为准（按你们项目实际）。

### 5.1 从 ipmitool / redfishtool 抽象出的“通用格式”

这两类工具（`ipmitool` 与 `redfishtool`）的共同点是：

- 全局连接参数：host、user、password、interface/protocol、超时/重试、TLS 策略
- 操作是“动词/资源”式：`power`、`sensor`、`sel`、`raw`/`get`/`list` 等
- 批处理靠脚本：将目标与命令外置到文件或管道
- 可靠自动化需要：稳定 JSON、清晰 exit code、可控重试/超时、脱敏日志

据此，bmcli 建议采用一个明确的“执行模型”：

- `targets`：一次运行选择 1 个或 N 个目标
- `steps`：一次运行执行 1 条或 N 条命令（对每个目标按顺序执行）
- `runs`：一次运行重复执行 1 次或多次（用于轮询/压测/观察窗口）

### 5.2 统一 CLI 形态（单/多目标，单/多命令，单/多次执行）

#### 5.2.1 通用语法（建议）

```bash
bmcli [GLOBAL] target [TARGETS] run [RUN] do [COMMANDS]
```

其中：

- `GLOBAL`：输出/日志/并发/超时/重试等全局参数
- `TARGETS`：单目标或多目标选择器（命令行、文件、stdin、profile）
- `RUN`：执行次数与间隔（一次或重复）
- `COMMANDS`：单条命令或多条命令（内联或文件）

#### 5.2.2 目标选择（建议）

- 单目标：`--host <host[:port]>` +（可选）`--protocol redfish|ipmi|auto`
- 多目标：`--targets <file>`（支持 `.txt/.csv/.yaml/.json` 其一，项目定稿后写死）
- 管道：`cat targets.txt | bmcli --targets - ...`（`-` 表示 stdin）
- profile：`--profile <name>`（设置默认协议/TLS/超时/凭据来源）

#### 5.2.3 命令选择（建议）

- 单条命令（最常见）：直接跟子命令树，如 `power status`
- 多条命令（同一次运行里串行执行）：`--cmd "power status" --cmd "health summary"`
- 命令文件（推荐用于复杂批处理）：`--cmd-file commands.yaml` 或 `commands.json`

命令文件建议字段：

- `commands[]`：`name`、`argv`（数组或字符串）、`continue_on_error`（默认 false）
- `env`：可选，用于覆盖某些运行时参数（但不得包含明文凭据）**[待确认]**

#### 5.2.4 单次/多次执行（建议）

- 单次：默认 1 次
- 重复 N 次：`--repeat <N>`
- 按间隔重复：`--every 10s --repeat 30`（共执行 30 次，每次间隔 10 秒）
- 直到条件满足：`--until-success --max-attempts 20 --every 5s` **[待确认]**

#### 5.2.5 并发模型（建议）

- `--concurrency <N>`：目标维度并发（对 N 个目标并行跑同一组命令）
- 命令维度默认串行：同一目标内多条命令按顺序执行，避免“依赖状态”的竞态
- `--per-target-concurrency <N>`（如确有需要）**[待确认]**

#### 5.2.6 输出与结果结构（建议）

- `-o table|json`：默认 table；脚本解析用 json
- 多目标 + 多命令时 JSON 建议结构：
  - `run_id`、`started_at`、`ended_at`
  - `targets[]`：`target`、`ok`、`results[]`（每条命令的 `cmd`、`ok`、`data`、`error`、`duration_ms`）

```bash
# 单目标：电源状态
bmcli --host 10.0.0.12 --user admin power status -o json

# 批量：从文件读取，批量上电（并发 20）
bmcli --targets targets.txt power on --concurrency 20

# 传感器与健康
bmcli --host 10.0.0.12 sensor list
bmcli --host 10.0.0.12 health summary -o json

# SEL 导出与清理
bmcli --host 10.0.0.12 sel list -o json > sel.json
bmcli --host 10.0.0.12 sel clear --yes

# 多目标 + 多命令：对每个目标按顺序执行两条命令
bmcli --targets targets.yaml --concurrency 20 \
  --cmd "power status" \
  --cmd "health summary -o json"

# 多目标 + 命令文件 + 重复执行：每 30s 执行一次，共 10 次
bmcli --targets targets.yaml --cmd-file commands.yaml --every 30s --repeat 10
```

---

## 6. 输出、错误码与可用性规范

### 6.1 输出
- 默认 `table`（便于人工排障）
- `-o json`（脚本解析，字段稳定）
- 错误输出到 stderr；结果输出到 stdout

### 6.2 错误码（建议）
- 0 成功
- 2 CLI 参数/用法错误
- 3 鉴权失败
- 4 目标不可达/连接失败
- 5 协议/响应解析失败（兼容性问题）
- 6 操作被拒绝/状态不允许
- 7 超时

### 6.2.1 多目标执行的退出码语义（建议）

- 默认：只要存在任一目标失败，整体退出码为非 0。
- `--ignore-errors`：即使存在目标失败，整体退出码仍为 0（用于“尽力而为”的巡检批处理）**[待确认]**
- `--fail-fast`：首个失败即停止剩余目标并返回对应错误码 **[待确认]**

### 6.3 安全要求
- debug 日志不得打印密码/token
- 支持 `--insecure` 时给出醒目警告
- 支持凭据轮换建议与最小权限账户

---

## 7. 非功能需求（NFR）
- 兼容 Linux（必须），macOS（建议）；Windows **[待确认]**
- 并发性能：支持 100+ 目标批量巡检（按网络与 BMC 性能调优）
- 可观测性：每次请求输出可选 request-id/耗时统计（脱敏）

---

## 8. 依赖与约束（待确认）
- 协议栈：Redfish（HTTPS/JSON）、IPMI（lanplus）是否都支持？
- 厂商差异：OEM 字段、SEL/传感器命名差异、固件升级流程差异
- 网络约束：BMC 网段隔离、跳板机/代理、TLS 证书策略

---

## 9. 验收用例清单（建议）

- `power status`：返回当前电源状态，JSON 输出包含 `target`、`ok`、`data.power_state` 等稳定字段 **[待确认]**。
- `power on` 幂等：已开机再次执行仍返回成功，且状态可被 `power status` 复核。
- `power off` 二次确认：未加 `--yes` 时必须提示确认；加 `--yes` 时无交互执行。
- `sensor list`：输出可读表格；JSON 输出字段稳定，且能标识每项健康/告警等级 **[待确认]**。
- `sel list`：能拉取事件并按时间排序；不可解析的 OEM 字段不应导致整条记录丢失（应降级为原始字段）**[待确认]**。
- `sel clear`：执行后 `sel list` 结果为空或仅包含新事件（按 BMC 实现差异允许短暂延迟）**[待确认]**。
- 多目标并发：`--concurrency 20` 时能稳定完成 100 个目标巡检；单目标失败不影响其他目标结果产出。
- `--timeout`：超时必须返回错误码 7，并在输出中明确是连接超时还是操作超时 **[待确认]**。
- `--debug` 脱敏：任何情况下不得输出明文 password/token（包含 URL、header、body、log file）**[待确认]**。
- 多命令串行：同一目标下 `--cmd A --cmd B` 必须按顺序执行并分别产出结果（包含耗时与错误信息）。
- 重复执行：`--every 10s --repeat 3` 必须执行 3 次，时间间隔允许有抖动但需要可观测（输出包含 run 序号与时间戳）**[待确认]**。
- 失败策略：默认任一目标失败整体退出码非 0；`--ignore-errors` 时退出码仍为 0 **[待确认]**。

## 10. 待确认清单（从文内汇总）

- 二进制命令名：`bm` 还是 `bmcli`，以及子命令树的最终命名。
- 协议支持范围：Redfish / IPMI / 厂商私有接口的优先级与覆盖面。
- SOL/KVM/虚拟介质：是否纳入 V1.1/V2.0，具体能力边界与厂商差异处理策略。
- 固件升级：是否支持、异步任务模型（task id / wait）、失败/回滚策略。
- 健康汇总的维度：CPU/内存/风扇/电源/温度/存储等的可得性与字段映射。
- SEL 导出格式：JSON/CSV 的字段定义与编码（时区、时间格式）。
- 配置文件与凭据存储：是否支持 `credential_ref`、是否集成 OS keyring、env/flags/config 的优先级。
- 多目标执行策略：`--ignore-errors`、`--fail-fast` 是否要做，退出码与汇总输出的最终语义。
- 平台兼容性：Linux/macOS/Windows 的支持范围。

## 11. 需要你补充以“贴合现状”的信息

为了把这份 PRD 从“建议版”收敛为“项目真实版”，建议补齐：

1. `bmcli --help` 或 `bm --help` 输出（命令树）
2. 当前已支持的协议与厂商范围（例如仅 Redfish 标准路径，或包含 OEM）
3. 现有配置文件格式/字段（host、user、auth、profile 等）与默认路径

基于以上信息，可以进一步把「命令名称/参数/返回字段/验收用例」全部改成与你们实现 1:1 对齐的版本。
