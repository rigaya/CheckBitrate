
# CheckBitrate
Output bitrate distribution of video file(s) in csv format.

**[日本語版はこちら＞＞](./Readme.ja.md)**  

## Download
[github releases>>](https://github.com/rigaya/CheckBitrate/releases)

## System Requirements  
Windows 10 (x86/x64)  
Linux

## Usage
```bat
CheckBitrate.exe　[Options] <Video File 1> [<Video File 2>]...
```
The bitrate distribution will be written in &lt;Video File Name&gt;.trackID.bitrate.csv.

### Options

_-i &lt;float&gt;_  
Set bitrate distribution resolution in seconds. Due to frame rate, the might be a case that the value is exactly applied.  

The default value is automatically set between 0.5 - 4.0 seconds, depending on the duration of the file.

## Example of the output file
[output example (csv)](./example/example.csv)  
![graph from csv](./example/example.png "example")

## Precautions for using CheckBitrate  
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
