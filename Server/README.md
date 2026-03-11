# WIP readme for explaining the servers

## server.cpp

## httpServer.cpp

The files are loaded into a server through an xml file that lists the files to serve and needed information about the files. The xml file looks like this.

This is the head tag of the file. The open tag must be the first tag in the file and the close tag must be the last. All file entries go in-between these tags.
```
<list>
    ...
</list>
```

This is the file tag. It represents a file that the server will serve to the web browser.
```
<file path="File_on_Disk" uri="Public_Access_Name" mime="Mime_Type" cache="true/false/Number" default="true/false" />
```
path: The phisical location of the file on the server.
uri: The path or name that a browser will put in an address.
mime: MIME type of the accociated file. This is needed for browsers to use the file as it is intended to. Usually based off of the extention of the file. A quick Google search will tell you what the mime type is for a given extention.
cache: Weather or not the file should be stored on the computer the browser is using. This is true by default. If you expect the contents of a file to ever change, set this to false. Otherwise your app can appear broken to the end user. A number can be passed here if the file should only be stored for a set amount of seconds. This is useful for the emscripten generated .js file which the browser will request multiple times. Once for the inital load and once for every audio worklet and wasm worker that is created in the program.
default: If true, this file will be served to the browser if the "/" url uri is requested. This is false by default so it can be ommited on any file tag that is 'default="false"'.
