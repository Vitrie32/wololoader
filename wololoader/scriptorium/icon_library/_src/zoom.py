import sys, subprocess, io
from PIL import Image
ROOT='<repo>/wololoader/scriptorium/icon_library'
cat=sys.argv[1]; names=sys.argv[2:]
cols=6; sz=128
sheet=Image.new('RGB',(cols*(sz+4)+4,((len(names)+cols-1)//cols)*(sz+4)+4),(50,50,50))
for i,n in enumerate(names):
    png=subprocess.run(['rsvg-convert','-w',str(sz),'-h',str(sz),f'{ROOT}/{cat}/{n}.svg'],capture_output=True).stdout
    im=Image.open(io.BytesIO(png)).convert('RGBA'); sheet.paste(im,(4+(i%cols)*(sz+4),4+(i//cols)*(sz+4)),im)
sheet.save('/tmp/claude-1000/-home-ali-Masa-st--AOE2--edit/7fb50b8b-a7ae-4f4d-8b30-a0f301f7b601/scratchpad/zoom.png')
