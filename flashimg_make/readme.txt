简介
=========
使用python编写的脚本根据配置文件ImageInfo.ini生成镜像文件，需要python2.7版本。


使用方法
==========
参考当前目录下ImageInfo.ini文件，填写信息，格式如下：

[code1]
FilePath=文件名
ImageName=镜像文件名称
FlashAddress=FLASH地址，16进制格式
MemoryAddress=内存加载地址，16进制格式
Entry=代码运行入口地址，16进制格式

[param1]
FilePath=文件名
ImageName=镜像文件名称
FlashAddress=FLASH地址，16进制格式

各部分解释如下：
--------------
[code1]段描述含有可执行代码的镜像文件的信息，可以是ramboot、uboot、uImage等其他用户自己生成的镜像

[param1]段描述有配置参数生成的镜像文件的信息，例如ISP图像效果参数

* FilePath：镜像文件路径和文件名
* ImageName：会被bootloader利用打印解析到的镜像文件名
* FlashAddress：该镜像文件要写入到flash的地址
* MemoryAddress：该镜像文件加载到内存的地址
* Entry：该镜像文件运行的入口地址

可以添加多个code段、param段，使用序号区别。
最好按照序号顺序排列各段，code段和param段可以穿插书写。工具在制作最终flash镜像文件时会按照FlashAddress顺序重新排列各段。

每段内的属性描述参考上面，例如：

[code1]
...

[param1]
...

[param2]
...

[code2]
...

[code3]
...


2.将需要的bin文件放到和FilePath吻合的目录中

3.运行python脚本，开始生成



其他
============
生成的Flash.img将代码段按flash地址对齐合并在一起，其中头部做4096字节对齐。


调试开发
------------
针对ROMBOOT调试开发，可能不希望镜像文件Flash.img写入到Flash芯片中，可以在ImageInfo.ini中加入：
[productroot]
WriteFlash = No


可以加入如下选项生成可以被ROMBOOT解析的xmodem.img：
[code1]
Xmodem = Yes
...

[param1]
Xmodem = Yes
...

Xmodem：Yes表示该镜像文件要加入到最终生成的xmodem.img; No表示不加入到最终生成的xmodem.img
生成的xmodem.img将所有代码段合在一个文件中，通过xmodem协议发送。其中头的部分做128字节对齐，代码段之间也做128字节对齐