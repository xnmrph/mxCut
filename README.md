<p align="center"><img src="packaging/branding/banner.png" alt="mx" width="100%"></p>

# mxCut

Trim videos quickly, one at a time or a whole folder at once. mxCut shows a
preview of the video, you mark where to start and stop, and it saves the cut.
FFmpeg is built in, so there is nothing else to install.

**[Download mxCut](https://github.com/xnmrph/mxCut/releases/latest)** for
Windows or Linux. See **[INSTALL.md](INSTALL.md)** for step-by-step
instructions.

## How to use it

### Trim one video

1. Click **Open video** or drag a video onto the window.
2. Move to where the cut should start and press **A**. Move to where it should
   end and press **D**. You can also drag the `<` and `>` tabs on the timeline.
3. Pick **Fast** or **Accurate** (see below) and click **Trim**.

The trimmed video is saved next to the original, with `_trim` added to its
name.

### Keep several parts of a video

Press **+** next to the `[ ]` buttons, or **S**, to add another part. Each part
gets its own colour, up to five. Click a part's box at the bottom to choose
which one A and D change, and click **×** to remove it. When you click
**Trim**, the parts are joined into one video.

### Trim a whole folder

1. Click **Open folder** or drag a folder onto the window. Its videos are
   listed on the left.
2. For each video you want, mark the cut and press **Add to queue** (**Q**).
   mxCut moves on to the next video.
3. Under the queue, rename any video's output if you like, and use
   **Change…** under **Save to** to pick the folder they go in.
4. Click **Start trimming**. Each video is saved as its own file.

The export queue is there for a single video too: press **Add to queue** to
rename it before trimming.

### Fast or Accurate

- **Fast** finishes in seconds and keeps full quality, but the cut can start a
  little before the point you marked.
- **Accurate** cuts on the exact frame you marked. It takes longer because the
  video is re-encoded.

## Keyboard shortcuts

| Key | What it does |
| --- | --- |
| Space | Play / pause |
| A / D | Set the start / end of the cut |
| ← / → | One frame back / forward |
| Shift + ← / → | 5 seconds back / forward |
| [ / ] | Jump to the start / end of the cut |
| S | Add another part |
| Q | Add to queue |
| ↑ / ↓ | Previous / next video (folder) |
| Ctrl + O | Open a video |
| Ctrl + Shift + O | Open a folder |
| Ctrl + , | Settings |

## Settings

Click the gear in the top right corner to change:

- **Where trims are saved** and how they're named.
- **Replace the original video**: the trimmed video takes the original's place,
  and the original goes to the Recycle Bin or Trash.
- **Keyboard shortcuts** for setting the start and end (A and D by default).
- **Accurate mode quality**, including faster encoding on NVIDIA, Intel or AMD
  graphics cards when yours supports it.

If a trim fails, mxCut says what went wrong. Hover over the message for the
details.

## For developers

Building from source, releasing and how the code is laid out are in
[BUILDING.md](BUILDING.md).
