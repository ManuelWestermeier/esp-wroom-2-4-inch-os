# ESP 32 WROOM 2.4 inch display os

8059 LINES OF CODE

```txt

-GLOBALS:

HASH = Sha256
ENC = AES 256 + IV

-ENTRY:

GET USER_NAME, USER_PASSWORD

UID = HASH(USER_NAME)
PSS = HASH(USER_PASSWORD)

/GLOBAL/ = UNENCRYPTED FILES GLOBAL
ROOT_PATH = /{UID}/

ROOT_PATH/os/password == PSS
=>

ROOT_PATH/os/settings/
ROOT_PATH/os/env/urls
ROOT_PATH/os/env/default
ROOT_PATH/os/env/paths
ROOT_PATH/os/env/open
ROOT_PATH/os/apps/
ROOT_PATH/os/libs/

ROOT_PATH/user/home/
ROOT_PATH/user/docs/
ROOT_PATH/user/img/
ROOT_PATH/user/vid/
ROOT_PATH/user/audio/

OPEN HOME

-APPS:

ROOT_PATH/os/apps/{APP-NAME}/
    app.exe.lua
    app.name
    app.icon
    app.shortcuts
    app-data/

```
