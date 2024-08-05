# deePixelPress

deePixelPress is a powerful and user-friendly image compression tool with a GUI interface, designed to help you optimize your images efficiently.

## Features

- Simple and intuitive GUI for easy image compression
- Support for both JPEG and PNG formats
- Multiple compression levels:
  - JPEG: Low, Medium, High, Ultra
  - PNG: Low, Medium, High
- Batch processing capability
- Drag and drop support for adding files
- Preview of original and compressed images
- Display of image metadata
- Option to remove metadata from compressed images
- File size savings calculation and display
- Customizable interface with keyboard shortcuts

## Requirements to Build

- GTK+ 3.0 development libraries
- FFmpeg
- GCC compiler

## Build Instructions

To build deePixelPress, use the following GCC command:
gcc -o deePixelPress deePixelPress.c $(pkg-config --cflags --libs gtk+-3.0) -lm

Make sure you have the necessary development libraries installed before building.

## Usage

1. Launch deePixelPress
2. Add images by clicking the "Add" button or dragging and dropping files
3. Select an image from the list
4. Choose the desired compression level
5. Click "Compress" to process the image
6. Use "Save" to keep the compressed image or "Remove Metadata & Save" to strip metadata before saving

## Keyboard Shortcuts

- Ctrl+A: Add files
- Ctrl+C: Compress selected image
- Ctrl+S: Save compressed image
- Left Arrow: Decrease compression level
- Right Arrow: Increase compression level

## Credits

deePixelPress uses FFmpeg for image processing. FFmpeg is a powerful multimedia framework that allows for the encoding, decoding, and manipulation of various audio and video formats, including images.

FFmpeg: https://ffmpeg.org/

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.