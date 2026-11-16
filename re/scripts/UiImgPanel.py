# Evidence dump for the image-panel recipe (FINDINGS "Mods ekrani v2"): checks ret N of every function in the chain.
# usage: UiImgPanel.py <full.asm>
import subprocess, sys
fns = {'LoadImage 0x49f1f0': '49f1f0', 'LoaderChain::Load 0x49ee10': '49ee10', 'DrawablePanel ctor 0x47a5d0': '47a5d0',
       'ImageFileDrawable ctor 0x475730': '475730', 'drawable slot3 SetFit 0x476e80': '476e80', 'drawable slot4 GetSize 0x4761e0': '4761e0',
       'scale rect 0x491190': '491190', 'SolidColorDrawable ctor 0x4a0350': '4a0350', 'SetChecked 0x477450': '477450',
       'SetEnabled(btn slot61) 0x479840': '479840', 'TextPanel SetText 0x497040': '497040', 'TextPanel SetText2 0x495f40': '495f40',
       'label factory 0x48fe60': '48fe60', 'label 0x48ff40': '48ff40'}
for k, v in fns.items():
    out = subprocess.run([sys.executable, '-I', __file__.replace('UiImgPanel', 'UiRet'), sys.argv[1], v], capture_output=True, text=True).stdout.strip()
    print('%-38s %s' % (k, out))
