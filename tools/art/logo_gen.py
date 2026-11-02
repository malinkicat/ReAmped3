"""ReAmped 3 logo candidates through the local ComfyUI (FLUX.1 schnell Q8, 4 steps).

Run on Windows with the ComfyUI install's Python (see the AC Career project's scripts/art/comfyui-win.sh py):
  logo_gen.py [style,...] [seed,...]
Writes <LOGO_OUT>/<style>-<seed>.png and prompts.json.
"""
import json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8188"
COMFY = os.environ.get("FLUXCAR_COMFY", r"D:\AC-DEV\comfyui-win\ComfyUI_windows_portable\ComfyUI")
OUT = os.environ.get("LOGO_OUT", r"D:\AC-DEV\scratch\reamped3-logo")
SEEDS = [11, 22, 33, 44, 55, 66]
TEXT = ('the bold title text "ReAmped 3" in big chunky angular blocky italic letters, hot pink and magenta letters '
        'with a thick black outline and a deeper magenta 3D extrusion underneath, ')
STYLES = {
    "stars": ("A 2000s extreme snowboarding video game logo: " + TEXT +
              "bright yellow five-pointed stars with pink outlines and pink comet streaks bursting out behind the "
              "letters on both sides, tilted slightly upwards to the right, energetic punk sticker style, flat vector "
              "colours, crisp clean edges, isolated on a perfectly flat uniform solid black background"),
    "sticker": ("A retro snowboard sticker logo for a video game: " + TEXT +
                "a white sticker border around the whole logo, two small yellow stars, slight upward tilt, flat vector "
                "art, crisp clean edges, isolated on a perfectly flat uniform solid black background"),
    "snow": ("A 2000s snowboarding video game title logo: " + TEXT +
             "icy white snow spray and small yellow stars flying behind the letters, a hint of a snowy mountain peak "
             "silhouette in cyan behind, flat vector colours, crisp clean edges, isolated on a perfectly flat uniform "
             "solid black background"),
}


def graph(prompt, seed, prefix, w=1344, h=768):
    return {
        "1": {"class_type": "UnetLoaderGGUF", "inputs": {"unet_name": "flux1-schnell-Q8_0.gguf"}},
        "2": {"class_type": "DualCLIPLoaderGGUF", "inputs": {"clip_name1": "t5-v1_1-xxl-encoder-Q8_0.gguf",
              "clip_name2": "clip_l.safetensors", "type": "flux"}},
        "3": {"class_type": "VAELoader", "inputs": {"vae_name": "ae.safetensors"}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt, "clip": ["2", 0]}},
        "5": {"class_type": "CLIPTextEncode", "inputs": {"text": "", "clip": ["2", 0]}},
        "6": {"class_type": "EmptySD3LatentImage", "inputs": {"width": w, "height": h, "batch_size": 1}},
        "7": {"class_type": "KSampler", "inputs": {"model": ["1", 0], "positive": ["4", 0], "negative": ["5", 0],
              "latent_image": ["6", 0], "seed": seed, "steps": 4, "cfg": 1.0, "sampler_name": "euler",
              "scheduler": "simple", "denoise": 1.0}},
        "8": {"class_type": "VAEDecode", "inputs": {"samples": ["7", 0], "vae": ["3", 0]}},
        "9": {"class_type": "SaveImage", "inputs": {"images": ["8", 0], "filename_prefix": prefix}},
    }


def post(path, data=None):
    req = urllib.request.Request(API + path, data=json.dumps(data).encode() if data is not None else None,
                                 headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req))


def run(name, g):
    dst = os.path.join(OUT, name + ".png")
    if os.path.exists(dst):
        return
    t0 = time.time()
    pid = post("/prompt", {"prompt": g})["prompt_id"]
    while True:
        h = post(f"/history/{pid}")
        if pid in h:
            st = h[pid].get("status", {})
            if st.get("status_str") == "error":
                print(name, "ERROR", json.dumps(st)[-400:], flush=True)
                return
            if h[pid].get("outputs"):
                break
        time.sleep(0.5)
    img = h[pid]["outputs"]["9"]["images"][0]
    shutil.move(os.path.join(COMFY, "output", img["subfolder"], img["filename"]), dst)
    print(f"{name}: {time.time() - t0:.1f}s", flush=True)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    styles = sys.argv[1].split(",") if len(sys.argv) > 1 else list(STYLES)
    seeds = [int(v) for v in sys.argv[2].split(",")] if len(sys.argv) > 2 else SEEDS
    with open(os.path.join(OUT, "prompts.json"), "w") as f:
        json.dump({"styles": STYLES, "seeds": seeds}, f, indent=1)
    for s in styles:
        for sd in seeds:
            run(f"{s}-{sd}", graph(STYLES[s], sd, "reamped3/" + s))
