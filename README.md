# About this repository
This is a "patch" for Epic's official Maya Live Link plugin.

* Adds support for Maya 2015 SP6.
* Adds support for keyframed custom attributes on the root joint. (keyframes are mandatory for values to be sent)
* No other changes. Use the plugin as you usually would.

Either copy the files from the Binaries folder or follow the instructions below to compile from scratch.

# Compiling for versions newer than Maya 2015
Just copy the .cpp file and replace it in the official repository. It has not been tested for Maya 2016 and above, but works for 2015.

# Compiling for Maya 2015
***Please note:*** The build will fail, but the DLL will still be generated.

* Download the Unreal Engine repository and follow the installation steps on their Github
* Go to `\Engine\Source\Programs\MayaLiveLinkPlugin`
* Replace the files with the ones in this repository
* Run `\Engine\Source\Programs\MayaLiveLinkPlugin\BuildMayaPlugin.bat`
* Wait for `MayaLiveLinkPlugin2015.dll` to appear in `\Engine\Binaries\Win64`
* Rename `MayaLiveLinkPlugin2015.dll` to `MayaLiveLinkPlugin2015.mll`
* Copy these files 
    * `\Engine\Binaries\Win64\MayaLiveLinkPlugin2015.mll`
    * `\Engine\Source\Programs\MayaLiveLinkPlugin\MayaLiveLinkUI.py` 
    * to
    * `C:\Program Files\Autodesk\Maya2015\bin\plug-ins`