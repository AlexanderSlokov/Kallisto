
# Antigravity server symlink workaround
# Bug: Antigravity looks for node at .../bin/{commit}/node
# but server installs to .../bin/{version}-{commit}/node
# Lệnh này lừa IDE bằng cách tạo một đường dẫn giả trỏ về đường dẫn thật

```bash
ln -s \
  /home/vscode/.antigravity-server/bin/1.16.5-1504c8cc4b34dbfbb4a97ebe954b3da2b5634516 \
  /home/vscode/.antigravity-server/bin/1504c8cc4b34dbfbb4a97ebe954b3da2b5634516
```