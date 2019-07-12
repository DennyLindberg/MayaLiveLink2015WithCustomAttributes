# About this repository
This is a "patch" for Epic's official Maya Live Link plugin.

* Adds support for Maya 2015 SP6.
* Adds support for user defined attributes on the root joint.
* No other changes. Use the plugin as you usually would.

Either copy the files from the Binaries folder or follow the instructions below to compile from scratch.

# Compiling for versions newer than Maya 2015
Copy all the files except the binaries and replace in the MayaLiveLinkPlugin folder. Compile as usual.

Please note that you must download the devkit from Autodesk to have the mandatory headers and lib files. Otherwise you can't compile the plugin for Maya 2016 and above.
https://www.autodesk.com/developer-network/platform-technologies/maya

# Compiling for Maya 2015 and UE4 4.23
* Download the Unreal Engine repository and follow the installation steps on their Github
* Go to `\Engine\Source\Programs\MayaLiveLinkPlugin`
* Replace the files with the ones in this repository
* Run `\Engine\Source\Programs\MayaLiveLinkPlugin\BuildMayaPlugin2015.bat`
* Compiled files end up in `\Engine\Source\Programs\MayaLiveLinkPlugin\output`
* Copy them to `C:\Program Files\Autodesk\Maya2015\bin\plug-ins`

# Compiling for Maya 2015 and UE4 4.22
***Please note:*** The build will fail, but the DLL will still be generated.

* Download the Unreal Engine repository and follow the installation steps on their Github
* Go to `\Engine\Source\Programs\MayaLiveLinkPlugin`
* Replace the files with the ones in this repository
* Run `\Engine\Source\Programs\MayaLiveLinkPlugin\BuildMayaPlugin2015.bat`
* Wait for `MayaLiveLinkPlugin2015.dll` to appear in `\Engine\Binaries\Win64`
* Rename `MayaLiveLinkPlugin2015.dll` to `MayaLiveLinkPlugin2015.mll`
* Copy these files 
    * `\Engine\Binaries\Win64\MayaLiveLinkPlugin2015.mll`
    * `\Engine\Source\Programs\MayaLiveLinkPlugin\MayaLiveLinkUI.py` 
    * to
    * `C:\Program Files\Autodesk\Maya2015\bin\plug-ins`