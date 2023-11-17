# astlib
Eurocontrol Asterix decoder framework based on XML declarations

Project is inspired by: https://github.com/vitorafsr/asterixed

Dependencies
- Modern C++11 compiler, gcc 5.x, Visual Studio 2015
- Cmake 3.3 or above
- Poco library 1.7.0 or above
- Gtest (optional)
- Node.js (optional) (+v8pp +mocha)

## Peralex Guide
The original project used cmake to generate the build system. This can be used to generate an VS2019 project to start development, or make an official build of the library. The steps are provided below.
### Build
**Windows x64 Debug**
1. mkdir Debug
2. cd Debug
3. cmake -DCMAKE_BUILD_TYPE=Debug ..
4. cmake --build . --config Debug

**Windows x64 Release**
1. mkdir Release
2. cd Release
3. cmake -DCMAKE_BUILD_TYPE=Release ..
4. cmake --build . --config Release

Note that after step (3), you have the VS2019 project and can open it to develop and build the application. Step (4) is only necessary if you are not interested in an development environment and only need to build the application for the static library (`astlib.lib`). The static library file(s) can be found in `<build-config>/lib/bin/..` where `<build-config>` is either `Debug` or `Release`.

If you want to make an official build (after making any changes to the library) make sure to update the version in `version.info` and commit this change. Then, go to the [repository](https://github.com/Peralex/astlib) and manually create a tag for that commit with the same name as the version. For example, the version, `1.00T001` should be present in `version.info` and there should be a tag with the same corresponding name.

Note that if you just want to make a new build of the software from a particular version, you can use the following command to retrieve a fresh copy of the repository in a new build directory:
```bash
git clone --depth 1 --branch <version> https://github.com/Peralex/astlib
```

## Original Guide
Building - Windows [Debug]
1. <checkout source somewhere into astlib directory>
2. mkdir debug
3. cd debug
4. cmake -G"Visual Studio 14 2015 Win64" -DCMAKE_BUILD_TYPE=Debug ..
5. msbuild astlib.sln  /t:Rebuild /p:Configuration=Debug
6. cd ..
7. bin/Debug/testunit.exe

Building - Windows [NMake][Debug]
1. <checkout source somewhere into astlib directory>
2. mkdir debug
3. cd debug
4. cmake -G"NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Debug ..
5. jom /j9
6. cd ..
7. bin/testunit.exe

Building - Windows [Release]
1. <checkout source somewhere into astlib directory>
2. mkdir release
3. cd release
4. cmake -G"Visual Studio 14 2015 Win64" -DCMAKE_BUILD_TYPE=Release ..
5. msbuild astlib.sln /t:Rebuild /p:Configuration=Release
6. cd ..
7. bin/Release/testunit.exe

Node.js support (or https://www.npmjs.com/package/node-cmake)
 1. install nodejs package
 2. npm install --save bindings
 2. sudo npm install -g cmake-js [https://www.npmjs.com/package/cmake-js]

 1. cmake-js build
