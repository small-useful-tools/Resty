# Resty

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

一个面向 Windows 的轻量级工作休息提醒工具，目标是：

- 尽量只产出一个 exe
- 无需安装
- 常驻托盘运行
- 支持固定日期、每日、每周循环等多段休息规则
- 用原生窗口实现小休息/大休息遮罩提醒

---

## 1. 产品目标

### 首页

- 展示“下次休息”倒计时
- 展示下一条休息规则摘要
- 支持手动预览小休息 / 大休息提醒

### 设置

#### 系统设置

- 开机自启动
- 托盘运行
- 关闭主窗口时最小化到托盘

#### 小休息设置

- 透明度
- 提示词
- 颜色
- 居中、非全屏、置顶遮罩提醒

#### 大休息设置

- 透明度
- 提示词
- 颜色
- 居中、全屏、置顶遮罩提醒

### 提醒规则

支持多段规则，优先覆盖下面几类：

- 每天固定时间
- 每周循环
- 固定日期

后续可扩展：

- 工作日模板
- 节假日排除
- 临时跳过本次提醒
- 番茄钟模式

---

## 2. 数据与配置目录

默认存储到当前用户目录下：

- 配置文件：`%USERPROFILE%\\.resty\\config.ini`
- 休息数据：`%USERPROFILE%\\.resty\\data\\rest.txt`

建议目录结构：

```text
%USERPROFILE%\.resty/
  config.ini
  data/
    rest.txt
  logs/
    app.log
```

### config.ini 规划

建议按功能分段：

```ini
[app]
auto_start=0
minimize_to_tray=1
language=zh-CN

[short_rest]
opacity=220
color=#1E40AF
message=站起来，活动肩颈和眼睛。

[long_rest]
opacity=235
color=#7F1D1D
message=离开工位，走动几分钟，真正休息一下。
```

### rest.txt 规划

优先使用“每行一条规则”的轻文本方案，便于单 exe 读写：

```text
short|daily|10:30|5
short|weekly|Mon,Tue,Wed,Thu,Fri|15:30|5
long|date|2026-05-01|14:00|15
```

说明：

- `short`：小休息
- `long`：大休息
- `daily`：每日固定时间
- `weekly`：每周循环
- `date`：固定日期
- 最后一段数字：本次休息时长（分钟）

---

## 3. 技术方案

### 选型原则

- 原生 C++ + Win32 API
- 不依赖 WebView / .NET 运行时
- 尽量静态链接，减少分发成本
- 使用 Visual Studio 自带工具链直接编译

### 当前建议栈

- UI：Win32 API + GDI
- 图标：本地 `.ico` 资源文件，可随时替换占位图
- 托盘：`Shell_NotifyIcon`
- 配置：INI 文件
- 规则存储：纯文本文件
- 定时调度：本地时间轮询 + 下次触发时间计算
- 开机自启：注册表 `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run`

这样更符合：

- 单 exe
- 轻量
- 可直接拷贝使用
- 不需要额外安装器

---

## 4. 模块拆分规划

用户建议按不同窗口、不同模块拆分，后续按下面结构重构：

```text
Resty/
  app/
    App.h
    App.cpp
  core/
    Scheduler.h
    Scheduler.cpp
    Rule.h
    Rule.cpp
    ConfigStore.h
    ConfigStore.cpp
    RestStore.h
    RestStore.cpp
    PathService.h
    PathService.cpp
  ui/
    MainWindow.h
    MainWindow.cpp
    SettingWindow.h
    SettingWindow.cpp
    OverlayWindow.h
    OverlayWindow.cpp
    TrayIcon.h
    TrayIcon.cpp
  platform/
    AutoStartService.h
    AutoStartService.cpp
  resources/
    resource.h
    Resty.rc
  main.cpp
```

### 模块职责

#### `App`

- 应用生命周期管理
- 单实例控制
- 初始化各模块
- 消息循环

#### `MainWindow`

- 首页展示
- 倒计时刷新
- “打开设置 / 预览提醒”入口

#### `SettingWindow`

- 系统设置
- 小休息设置
- 大休息设置
- 规则编辑与校验

#### `OverlayWindow`

- 小休息遮罩提醒
- 大休息全屏提醒
- 透明度、颜色、提示词渲染

#### `TrayIcon`

- 托盘图标
- 右键菜单
- 显示主页 / 设置 / 退出

#### `Scheduler`

- 规则解析
- 下次提醒时间计算
- 当前时刻命中判断

#### `ConfigStore`

- 读写 `config.ini`
- 默认配置初始化

#### `RestStore`

- 读写 `rest.txt`
- 规则文本与结构体互转

#### `PathService`

- 用户目录定位
- `.resty` 目录创建
- 路径集中管理

#### `AutoStartService`

- 设置/取消开机自启动
- 封装注册表操作

---

## 5. 窗口规划

### 5.1 MainWindow

用途：主页

核心区域：

- 下次休息倒计时
- 下一条休息摘要
- 当前规则数量
- 打开设置按钮
- 预览小休息按钮
- 预览大休息按钮

### 5.2 SettingWindow

用途：设置页

分组：

- 常规设置
- 小休息设置
- 大休息设置
- 规则编辑区

交互要求：

- 保存前做格式校验
- 校验失败定位问题
- 保存后立即刷新主页调度状态

### 5.3 OverlayWindow

用途：提醒页

#### 小休息

- 置顶
- 半透明遮罩
- 居中显示
- 非全屏

#### 大休息

- 置顶
- 半透明遮罩
- 居中显示
- 全屏覆盖

---

## 6. 开发阶段计划

### Phase 1：骨架与基础能力

- [x] 建立 VS C++ 工程
- [x] 接入 Win32 主窗口
- [ ] 拆分模块与头/源文件
- [ ] 统一路径服务
- [ ] 切换到 `.resty` 配置目录

### Phase 2：首页与设置页

- [ ] 首页倒计时
- [ ] 设置窗口基础布局
- [ ] 小休息/大休息配置保存
- [ ] 托盘菜单

### Phase 3：调度与提醒

- [ ] 每日/每周/固定日期规则解析
- [ ] 下次提醒计算
- [ ] 小休息遮罩提醒
- [ ] 大休息全屏提醒

### Phase 4：打磨与发布

- [ ] 图标与资源文件
- [ ] 异常处理与日志
- [ ] Release x64 构建验证
- [ ] 单 exe 分发说明

---

## 7. 编译方式

当前项目可使用以下 MSBuild 路径编译：

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe
```

建议优先构建：

- `Release | x64`

目标产物：

- 单个 `Resty.exe`

---

## 8. 下一步建议

下一步按下面顺序推进：

1. 先把当前单文件实现拆成 `MainWindow / SettingWindow / OverlayWindow / Scheduler / ConfigStore / RestStore`
2. 路径统一切换到 `%USERPROFILE%\\.resty`
3. 将规则读写从配置文件中迁移到 `data/rest.txt`
4. 再继续编译修复与功能补齐

如果继续开发，下一步直接做“模块拆分 + 路径落地”。
