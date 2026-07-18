# Reddy Xiaozhi Server

Reddy 的轻量级小智 WebSocket 服务端，与本仓库 `main` 分支中的
ESP32-S3-Touch-AMOLED-1.8 固件配套使用。

主要能力：

- 小智二进制协议 v3、STT、LLM、TTS；
- 带北京时间上下文、去重复保护和话题相关检索的长期记忆；
- LLM 输出私有表情标签，设备显示 21 种动态表情，标签不会进入语音；
- OTA 版本检查、固件托管、灰度发布和强制版本策略；
- OTA 发布时校验 ESP 固件项目名和内嵌版本；
- 网页管理对话、提示词、用户资料、记忆、同学录和 OTA。

## 安装

```bash
python -m venv venv_xiaozhi
source venv_xiaozhi/bin/activate
pip install -r requirements.txt
python -m xiaozhi.run
```

服务默认监听：

- WebSocket：`7070`
- 管理与 OTA HTTP：`7071`

## 配置

在项目根目录创建 `.env`。密钥和运行数据均已被 `.gitignore` 排除，
不要提交到 Git：

```dotenv
XZ_HOST=0.0.0.0
XZ_PORT=7070

LLM_API_KEY=replace-me
LLM_BASE_URL=https://api.deepseek.com/v1
LLM_MODEL=deepseek-chat

STT_PROVIDER=aliyun
ALIYUN_AK_ID=replace-me
ALIYUN_AK_SECRET=replace-me
ALIYUN_NLS_APPKEY=replace-me

TTS_API_KEY=replace-me

OTA_PUBLIC_BASE_URL=http://your-host:7071
OTA_WEBSOCKET_URL=ws://your-host:7070/
OTA_ADMIN_TOKEN=use-a-long-random-token
```

## OTA 发布

可在 `http://your-host:7071/ota` 上传固件，也可使用命令行：

```bash
python publish_firmware.py build/xiaozhi.bin \
  --server http://your-host:7071 \
  --token "$OTA_ADMIN_TOKEN" \
  --version 2.3.0 \
  --board esp32-s3-touch-amoled-1.8 \
  --notes "Reddy 2.3.0"
```

发布接口会拒绝错误项目、错误版本、非 ESP 应用镜像和超过 6 MiB 的包。
设备端还会再次校验文件长度、SHA-256、项目名和版本，并使用双 OTA 分区回滚。

## 测试

```bash
python -m unittest discover -s tests -v
```
