Simple Python script to view an already downloaded image file from AWS.
Code was downloaded from the net and combined with AI instructions, never thogourly analyzed. Should do the work, though!
Delete the png file (it is the output), and run again over the bin file
```python.exe .\view_raw_image.py --path .\111x133_782016807.bin```
Output
PS C:\TriCloudEdge\esp32-s3-websocket_server\bin_files_viewer> python.exe .\view_raw_image.py --path .\111x133_782016807.bin
-> Parsed dimensions from filename: 111x133
-> Reading local file: '.\111x133_782016807.bin'...
   ...Success! Read 29526 bytes.
-> Reconstructing image from raw RGB565 data...
All done! Displaying the image.
Image also saved as '.\111x133_782016807.png'
PS C:\TriCloudEdge\esp32-s3-websocket_server\bin_files_viewer>