<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 灵感收件箱

这个零依赖本地服务先接收两张录音卡的音频，再交给 AI/飞书处理。即使转写或飞书
暂时不可用，录音本身也会被保留下来。

## 启动

```bash
python3 server.py
curl http://127.0.0.1:8787/health
```

用 `AI_REC_ROOT` 指定音频保存目录，用 `AI_REC_PORT` 改监听端口。服务启动时会在同一局域网
广播 `_aipassport._tcp` mDNS 服务，卡片因此不需要固定电脑 IP；电脑的接收端标识会保存在
`AI_REC_ROOT/receiver.id`，不要把这个运行时文件提交到仓库。也可用 `AI_REC_SERVICE_NAME`
修改广播名称，或用 `AI_REC_RECEIVER_ID` 显式指定标识。
`AI_REC_AGENT_WEBHOOK` 是可选的：设置后，服务在确认音频已写入磁盘后，会把一条 JSON
录音事件 POST 给同一个 AI/飞书处理入口。

## 兼容接口

现有 Cardputer 固件继续使用原来的接口：

```text
POST /upload?name=<name>.wav
```

AI Passport 使用按序的 IMA-ADPCM 分片，最后结束一次会话：

```text
POST /v1/passport/sessions/<session>/chunks/<sequence>
POST /v1/passport/sessions/<session>/complete
```

结束时生成的会话清单是统一交给 AI/飞书流水线的数据：其中记录来源设备、音频格式、本地
路径和分片顺序。传输和 Agent 适配刻意分开，避免把凭据写进固件或源码。
