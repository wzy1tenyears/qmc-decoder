# QQMusic QMC Decoder（Fork 版）

这是基于上游项目修改的个人 fork 版本。

- 上游项目：<https://github.com/Presburger/qmc-decoder>
- 原项目作者与原始许可证归上游所有
- 本仓库主要针对 Windows 批量使用流程和 MP3 转换流程做了调整

这不是上游官方发布仓库。

## 与上游差异

这个 fork 目前主要有这些差异：

- 启动后会依次询问：
  1. 输入目录
  2. 解码输出目录
  3. MP3 输出目录
- 第三个目录留空时，只解码，不转 MP3
- 会保留输入目录下的相对目录结构
- 处理顺序改为：
  1. 先完成全部文件解码
  2. 再统一开始 MP3 转换
- MP3 转换改为调用 `ffmpeg`
- MP3 参数使用 `libmp3lame -q:a 0`
- 改进了 Windows 下中文、日文文件名的路径处理
- 交互模式结束后不会立刻关窗，方便查看日志

## 支持格式

- `.qmc3`
- `.qmc0`
- `.qmcflac`
- `.qmcogg`

## 输出说明

- 解码后的原始音频会输出到“解码输出目录”
- 如果填写了“MP3 输出目录”，程序会在全部解码完成后再批量转成 MP3
- MP3 使用 VBR 模式，参数为 `-q:a 0`

## 运行依赖

- CMake
- C++ 编译器
- `ffmpeg`

如果需要 MP3 转换，`ffmpeg` 需要满足以下任一条件：

- 已加入 `PATH`
- 放在 `qmc-decoder.exe` 同目录下

## 构建

### Windows

```bat
git clone <你的仓库地址>
cd <仓库目录>
git submodule update --init --recursive
mkdir build
cd build
cmake -G "NMake Makefiles" .. -DCMAKE_BUILD_TYPE=Release
nmake
```

### Linux / macOS

```bash
git clone <你的仓库地址>
cd <仓库目录>
git submodule update --init --recursive
mkdir build
cd build
cmake ..
make
```

## 用法

### 交互模式

直接运行：

```bash
qmc-decoder
```

然后按提示输入：

1. 输入目录
2. 解码输出目录
3. MP3 输出目录（留空则跳过 MP3 转换）

### 命令行模式

仅批量解码：

```bash
qmc-decoder INPUT_DIR OUTPUT_DIR
```

批量解码并在全部解码完成后再转 MP3：

```bash
qmc-decoder INPUT_DIR OUTPUT_DIR MP3_OUTPUT_DIR
```

单文件解码：

```bash
qmc-decoder /PATH/TO/SONG
```

说明：单文件模式当前只会在原文件旁边输出解码结果，不会进入 MP3 批量转换阶段。

## 说明

- 这个 fork 更偏向个人实际使用场景的整理版本
- 如果你需要上游历史、原始说明或官方 release，请查看上游仓库
- 某些解码后的 FLAC 文件在 `ffmpeg` 中可能仍会出现解码警告，但只要 `ffmpeg` 接受输入，通常仍可继续转码

## 许可证

许可证见 [LICENSE](LICENSE)。

上游项目地址：
<https://github.com/Presburger/qmc-decoder>
