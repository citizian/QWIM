# QWIM Stress Test Tool

独立的压力测试工具，用于测试远程 QWIM 服务器的性能和稳定性。

## 使用方法

```bash
# 一键运行（服务端已在另一台机器运行）
./run.sh <服务器IP> <服务器端口>

# 示例
./run.sh 192.168.1.100 8080
./run.sh 10.0.0.5 8080 /tmp/report.txt
```

## 测试内容

| 阶段 | 测试项 | 配置 | 时长 |
|------|--------|------|------|
| Phase 1 | 最大连接数 | 5000 连接, 不发消息 | 20s |
| Phase 2 | QPS 吞吐量 | 200 连接, 10 msg/s | 30s |
| Phase 3 | 高负载 | 100 连接, 50 msg/s | 30s |
| Phase 4 | 长期稳定性 | 500 连接, 5 msg/s | 60s |

总耗时约 3 分钟。

## 输出

- **终端**: 实时打印每秒状态（连接数、QPS、断连数）
- **报告文件**: 自动生成 `stress_report.txt`，包含所有测试数据

## 目录结构

```
stress_test/
├── CMakeLists.txt      # 独立构建配置
├── stress_tester.cpp   # 压测程序源码
├── run.sh              # 一键运行脚本
└── README.md           # 本文件
```

## 前置条件

- cmake >= 3.10
- g++ (支持 C++17)
- Linux 系统（使用 epoll）
