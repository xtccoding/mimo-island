# 🏝️ MiMo Island

MiMo Token 灵动岛悬浮窗 - 实时监控 API 用量

![Python](https://img.shields.io/badge/Python-3.10+-blue?style=flat-square&logo=python&logoColor=white)
![PySide6](https://img.shields.io/badge/PySide6-6.6+-green?style=flat-square&logo=qt&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)

## ✨ 特性

- 🎯 **灵动岛风格** - 仿 iPhone Dynamic Island 设计
- 🎨 **极致美学** - 玻璃拟态、流光边框、呼吸灯效
- 🔄 **弹簧动效** - 丝滑的展开/收起动画
- 📊 **实时监控** - API Token 用量一目了然
- 🔐 **多账号管理** - 支持多个 Cookie 配置切换
- ⚠️ **智能提示** - Cookie 过期自动红框警示
- 💾 **持久化存储** - 配置自动保存，重启不丢失

## 📦 安装

```bash
# 克隆项目
git clone https://github.com/yourusername/mimo-island.git
cd mimo-island

# 安装依赖
pip install -e .
```

## 🚀 使用

```bash
# 直接运行
python -m mimo_island.main

# 或使用安装后的命令
mimo-island
```

### 操作方式

| 操作 | 功能 |
|------|------|
| **左键点击** | 展开/收起 |
| **左键拖动** | 移动位置 |
| **右键点击** | 打开菜单 |

### 菜单功能

- **切换账号** - 在多个 Cookie 配置间切换
- **添加Cookie** - 粘贴 fetch/curl/纯字符串
- **删除当前** - 删除当前配置
- **刷新数据** - 手动刷新
- **置顶/取消置顶** - 窗口置顶控制

## 🔧 Cookie 获取方式

1. 打开浏览器，访问 `platform.xiaomimimo.com`
2. 按 `F12` 打开开发者工具
3. 切换到 `Network` 标签
4. 找到任意请求，右键 → `Copy` → `Copy as fetch`
5. 在悬浮窗右键 → `添加Cookie` → 粘贴

## 📁 项目结构

```
mimo-island/
├── src/
│   └── mimo_island/
│       ├── __init__.py
│       └── main.py          # 主程序
├── pyproject.toml            # 项目配置
├── README.md                 # 说明文档
├── LICENSE                   # MIT 许可证
└── .gitignore               # Git 忽略文件
```

## 🎨 设计规范

- **背景**: 深邃黑曜石 `#0E0E14`
- **主色**: 特斯拉蓝 `#468CFF`
- **辅色**: 极光青 `#22D3EE`
- **点缀**: 晚霞紫 `#A855F7`
- **字体**: 等宽数字 `JetBrains Mono` / UI `Segoe UI Variable`

## 📄 许可证

本项目采用 [MIT 许可证](LICENSE)。

## 🙏 致谢

- [PySide6](https://wiki.qt.io/Qt_for_Python) - Qt for Python
- [requests](https://docs.python-requests.org/) - HTTP 库
- iPhone Dynamic Island 设计灵感

---

Made with ❤️ by ctooc
