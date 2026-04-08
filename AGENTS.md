# Project Coding Rules (C++/Qt)

本文件用于约束本仓库的代码风格与实现规范。  
对本仓库进行任何代码修改时，必须遵守本文件规则。

## 1. Naming

- 类名：`UpperCamelCase`  
  示例：`RobotStatusWidget`
- 结构体名：`UpperCamelCase`  
  示例：`RobotPose`
- 函数名：`lowerCamelCase`  
  示例：`loadRobotConfig`
- 普通变量名：`lowerCamelCase`  
  示例：`currentRobotId`
- 成员变量名：`_lowerCamelCase`  
  示例：`_currentRobotId`
- 静态成员变量名：`s_lowerCamelCase`  
  示例：`s_instance`
- 常量名：`kUpperCamelCase`  
  示例：`kDefaultTimeoutMs`
- 宏名：`UPPER_SNAKE_CASE`  
  示例：`MAX_BUFFER_SIZE`
- 枚举类型名：`UpperCamelCase`  
  示例：`ConnectionState`
- 枚举值：`E_` 前缀 + `UpperCamelCase`  
  示例：`E_Disconnected`
- Qt 信号函数名：`sigXxx`  
  示例：`sigConnectionChanged`
- Qt 槽函数名：`slotXxx`  
  示例：`slotReconnect`

## 2. File And Type Layout

- 一个主要类对应一对文件：`.h/.cpp`，文件名与类名一致。
- 头文件使用 include guard 或 `#pragma once`，同一子模块保持一致。
- 头文件中禁止 `using namespace xxx;`。
- include 顺序：
  1. 本文件对应头文件
  2. Qt 头文件
  3. 标准库头文件
  4. 项目内其他头文件
- 能前置声明就前置声明，减少头文件耦合。

## 3. Function Rules

- 函数职责单一，优先早返回，减少嵌套层级。
- 单个函数建议不超过 80 行；超过时拆分私有辅助函数。
- 参数超过 4 个时，优先封装为结构体或配置对象。
- 避免魔法数字，提取为具名常量。
- 任何可能失败的操作必须有返回值检查或错误日志。

## 4. Qt Rules

- 能用新式连接就用新式连接：
  `connect(a, &A::sigXxx, b, &B::slotXxx);`
- QObject 对象优先通过父子关系管理生命周期。
- 非 QObject 资源优先使用 RAII（智能指针或栈对象）。
- UI 线程不做长耗时阻塞操作，耗时任务放工作线程。

## 5. Comment And Doc

- 对外接口（public API）必须有 Doxygen 注释。
- 注释解释“为什么”，避免只写“做了什么”。
- 复杂算法必须补充输入、输出、边界条件说明。

## 6. Logging And Error Handling

- 错误路径必须记录可定位日志（包含关键参数）。
- 日志内容应可检索，禁止无意义日志（如只打印“failed”）。
- 不要吞异常或忽略错误码。

## 7. Safety

- 禁止在未确认线程安全的情况下跨线程共享可变对象。
- 禁止引入未使用代码、未使用 include、未使用变量。
- 修改旧逻辑时保持行为兼容，除非需求明确要求变更行为。

## 8. Commit Quality Checklist

- 命名符合本规范。
- 编译通过。
- 关键路径已做最小验证（启动、主流程、异常路径）。
- 无明显无用代码、无调试残留。

## 9. Priority

- 若与历史代码风格冲突：新改动按本规范执行。
- 若与外部硬性规范冲突（编译器、框架、接口协议）：以硬性规范为准，并在注释中说明原因。
