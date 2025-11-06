function SetBoxSize(width)
{
	var box = document.querySelector('.box');
	box.style.width = width + 'px';
}

var resizeCodeReady = false;
	
function ResizeGameBox()
{
	var topPos = document.querySelector('.positionHelper').getBoundingClientRect();
	var gameBox = document.querySelector('.gameBox');
	var gameSizeBox = document.querySelector('.gameSizeBox');
	const availableScreenWidth = gameSizeBox.clientWidth;
	const availableScreenHeight = gameSizeBox.clientHeight - topPos.top - 8;
	if(availableScreenWidth < 0 || availableScreenHeight < 0)
	{
		gameBox.style.display = 'none';
	}
	else if((16/9) < (availableScreenWidth/availableScreenHeight))
	{
		gameBox.style.top = topPos.top + 'px';
		gameBox.style.bottom = 8 + 'px';
		const widthValue = (availableScreenWidth - (availableScreenHeight * (16/9))) / 2;
		gameBox.style.left = widthValue + 'px';
		gameBox.style.right = widthValue + 'px';
		gameBox.style.display = 'block';
	}
	else
	{
		const heightValue = (availableScreenHeight - (availableScreenWidth / (16/9))) / 2;
		gameBox.style.top = (topPos.top + heightValue) + 'px';
		gameBox.style.bottom = heightValue + 'px';
		gameBox.style.left = 8 + 'px';
		gameBox.style.right = 8 + 'px';
		gameBox.style.display = 'block';
	}

	if(resizeCodeReady == true)
	{
		_ResizeCall();
	}
}
	
function ShowGameBox()
{
	var gameBox = document.querySelector('.gameBox');
	gameBox.style.display = 'block';
	ResizeGameBox();
	window.addEventListener('resize', function(e){ResizeGameBox();});
}
	
function Step1()
{
	var box = document.querySelector('.box');
	box.innerHTML = '<div class="spinner"></div>';
	box.style.width = '150px';
	setTimeout(Step2, 2000);
}
	
function Step2()
{
	var box = document.querySelector('.box');
	box.style.transition = 'all 0.5s ease-in-out';
	box.style.width = '1000px';
	setTimeout(Step3, 500);
}
	
function Step3()
{
	var errors = [];
	var warnings = [];

	// Make sure WASM is enabled
	const wasmEnabled = function()
	{
		try
		{
			if(typeof WebAssembly === "object" && typeof WebAssembly.instantiate === "function")
			{
				const module = new WebAssembly.Module(Uint8Array.of(0x0, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00));
				if(module instanceof WebAssembly.Module)
					return new WebAssembly.Instance(module) instanceof WebAssembly.Instance;
			}
		}
		catch (e){}
		return false;
	}();

	if(!wasmEnabled)
		errors.push('The Classic Game Shuffle requires web assembly to be enabled. Please activate it and refresh the page.');

	// make sure web socket is enabled
	const webSocketEnabled = (typeof WebSocket !== 'undefined');
	if(!webSocketEnabled)
		errors.push('The Classic Game Shuffle requires WebSocket and it was not detected on your browser. Please make sure you can run WebSocket.');

	// Make sure WebGL is enabled
	const webGLEnabled = function ()
	{
		if(!!window.WebGLRenderingContext)
		{
			var canvas = document.createElement("canvas"),
				names = ["webgl2", "webgl", "experimental-webgl", "moz-webgl", "webkit-3d"],
				context = false;
			for(var i=0;i< names.length;i++)
			{
				try
				{
					context = canvas.getContext(names[i]);
					if(context && typeof context.getParameter == "function")
					{
						// WebGL is enabled
						return true;
					}
				}
				catch(e) {}
			}
			// WebGL is supported, but disabled
			return false;
		}
		// WebGL not supported
		return false;
	}();

	if(!webGLEnabled)
		errors.push('The Classic Game Shuffle requires WebGL and it was not detected on your browser. Please make sure you can run WebGL.');

	try
	{
		var AudioContext = window.AudioContext || window.webkitAudioContext;
		var ctx = new AudioContext();
		if(!(ctx.sampleRate == 44100 || ctx.sampleRate == 48000))
			warnings.push('Unsupported Audio Driver. The sound will be disabled.');
		ctx.close();
	}
	catch(e)
	{
		warnings.push('Unsupported Audio Driver. The sound will be disabled.');
	}

	var _emscripten_has_threading_support = () => typeof SharedArrayBuffer !== 'undefined';
	if(!_emscripten_has_threading_support())
		errors.push('The Classic Game Shuffle requires SharedArrayBuffer and it was not detected on your browser. Please make sure you can run SharedArrayBuffer.');

	if(errors.length == 0)
	{
		if(warnings.length == 0)
		{
			LoadWelcome();
		}
		else
		{
			var fullError = '<div class="warningText">';
			warnings.forEach(function(value, index, array)
			{
				fullError = fullError + String(value) + '<br><br>';
			});
			fullError = fullError + '</div>';
			fullError = fullError + '<button onclick="LoadWelcome()">Continue</button>';
			var box = document.querySelector('.box');
			box.innerHTML = fullError;
		}
	}
	else
	{
		var fullError = '<div class="errorText">';
		errors.forEach(function(value, index, array)
		{
			fullError = fullError + String(value) + '<br><br>';
		});
		fullError = fullError + '</div>';
		if(warnings.length != 0)
		{
			fullError = fullError + '<div class="warningText">';
			warnings.forEach(function(value, index, array)
			{
				fullError = fullError + String(value) + '<br><br>';
			});
			fullError = fullError + '</div>';
		}
		//fullError = fullError + '<button onclick="ShowGlobalSettings()">Continue anyway</button>';
		var box = document.querySelector('.box');
		box.innerHTML = fullError;
	}
}

function LoadWelcome()
{
	var box = document.querySelector('.box');
	box.innerHTML = '<div class="spinner"></div>';
	var welcome = new XMLHttpRequest();
	welcome.addEventListener("load", function() { var box = document.querySelector('.box'); box.innerHTML = this.responseText } );
	welcome.open("GET", "welcome.htm");
	welcome.send();
}
	
var GlobalSetting_Name = "";
var GlobalSetting_UseGamepad = false;
var GlobalSetting_A = "KeyZ";
var GlobalSetting_B = "KeyX";
var GlobalSetting_L = "KeyA";
var GlobalSetting_R = "KeyS";
var GlobalSetting_Left = "ArrowLeft";
var GlobalSetting_Right = "ArrowRight";
var GlobalSetting_Up = "ArrowUp";
var GlobalSetting_Down = "ArrowDown";

var GlobalTransientKBRebindIndex = 0;
var GlobalTransientControllerValid = false;
var GlobalTransientCheckControllerLoop = false;
var GlobalTransientControllerCache = [false, false, false, false, false, false, false, false];

function CheckControllerValidLoop()
{
	const controllers = navigator.getGamepads();
	if(controllers.length == 0)
	{
		GlobalTransientControllerValid = false;
		var img = document.getElementById("controllerIMG");
		if(img != null)
			img.className = "disableImage";
		else
			GlobalTransientCheckControllerLoop = false;
	}
	else
	{
		const c = controllers[0];
		if(c.mapping == "standard")
		{
			GlobalTransientControllerValid = true;
			var img = document.getElementById("controllerIMG");
			if(img != null)
				img.className = "fadeImage";
			else
				GlobalTransientCheckControllerLoop = false;
		}
		else
		{
			GlobalTransientControllerValid = false;
			var img = document.getElementById("controllerIMG");
			if(img != null)
				img.className = "disableImage";
			else
				GlobalTransientCheckControllerLoop = false;
		}
	}

	if(GlobalTransientCheckControllerLoop)
	{
		setTimeout(CheckControllerValidLoop, 100);
	}
}
	
function ShowGlobalSettings()
{
	var box = document.querySelector('.box');
	box.innerHTML = "Will you be playing with a keyboard or a controller?<br><div class='centerDIV'><img src='key.png' class='fadeImage' onclick='ShowKeyBind()'><img src='controller.png' id='controllerIMG' class='fadeImage' onclick='SelectController()'></div>";
	GlobalTransientCheckControllerLoop = true;
	CheckControllerValidLoop();
	//try {
	//GlobalSetting_Name = window.localStorage.getItem('CGS_NAME');
	//if(!GlobalSetting_Name) { GlobalSetting_Name = ""; }
	//GlobalSetting_UseGamepad = window.localStorage.getItem('CGS_GAMEPAD') === 'true';
	//}
	//catch(e) {}
	//var box = document.querySelector('.box');
	//box.innerHTML = 'Name:<br>This is the name that will appear on the leaderboards.<br>*NOTE: This feature is not implemented yet.<br><input type="text" id="NameTextbox" value="' + GlobalSetting_Name + '"><br><br><button onclick="CommitGlobalSettings()">Next</button><button onclick="RemoveGlobalSettings()">Reset</button>';
}

function GoToAfterWelcome()
{
	ShowGlobalSettings();
}

function ShowKeyBind()
{
	GlobalTransientCheckControllerLoop = false;
	var box = document.querySelector('.box');
	box.innerHTML = "To play on a keyboard, we need to set what keys will do what action. This is done by mapping the keys to a virtual controller.<br><br>This is the default.<br>A: " + GlobalSetting_A + "<br>B: " + GlobalSetting_B + "<br>L: " + GlobalSetting_L + "<br>R: " + GlobalSetting_R + "<br>Up: " + GlobalSetting_Up + "<br>Right: " + GlobalSetting_Right + "<br>Down: " + GlobalSetting_Down + "<br>Left: " + GlobalSetting_Left + "<br><br><div class='centerDIV'><button class='button' onclick='SelectKeyboard()'>Looks Good</button> <button class='button' onclick='KBS1()'>Custom Map</button> <button class='button' onclick='ShowGlobalSettings()'>Back to Select</button></div>";
	GlobalSetting_A = "KeyZ";
	GlobalSetting_B = "KeyX";
	GlobalSetting_L = "KeyA";
	GlobalSetting_R = "KeyS";
	GlobalSetting_Left = "ArrowLeft";
	GlobalSetting_Right = "ArrowRight";
	GlobalSetting_Up = "ArrowUp";
	GlobalSetting_Down = "ArrowDown";
}

function KBKeyDown(e)
{
	if(e.repeat) return;
	switch(GlobalTransientKBRebindIndex)
	{
	case 0: GlobalSetting_A = e.code; KBS2(); break;
	case 1: GlobalSetting_B = e.code; KBS3(); break;
	case 2: GlobalSetting_L = e.code; KBS4(); break;
	case 3: GlobalSetting_R = e.code; KBS5(); break;
	case 4: GlobalSetting_Up = e.code; KBS6(); break;
	case 5: GlobalSetting_Right = e.code; KBS7(); break;
	case 6: GlobalSetting_Down = e.code; KBS8(); break;
	case 7: GlobalSetting_Left = e.code; KBS9(); break;
	}
}

function KBS1()
{
	addEventListener("keydown", KBKeyDown);
	GlobalTransientKBRebindIndex = 0;
	var box = document.querySelector('.box');
	box.innerHTML = "A";
}

function KBS2()
{
	GlobalTransientKBRebindIndex = 1;
	var box = document.querySelector('.box');
	box.innerHTML = "B";
}

function KBS3()
{
	GlobalTransientKBRebindIndex = 2;
	var box = document.querySelector('.box');
	box.innerHTML = "L";
}

function KBS4()
{
	GlobalTransientKBRebindIndex = 3;
	var box = document.querySelector('.box');
	box.innerHTML = "R";
}

function KBS5()
{
	GlobalTransientKBRebindIndex = 4;
	var box = document.querySelector('.box');
	box.innerHTML = "Up";
}

function KBS6()
{
	GlobalTransientKBRebindIndex = 5;
	var box = document.querySelector('.box');
	box.innerHTML = "Right";
}

function KBS7()
{
	GlobalTransientKBRebindIndex = 6;
	var box = document.querySelector('.box');
	box.innerHTML = "Down";
}

function KBS8()
{
	GlobalTransientKBRebindIndex = 7;
	var box = document.querySelector('.box');
	box.innerHTML = "Left";
}

function KBS9()
{
	removeEventListener("keydown", KBKeyDown);
	var box = document.querySelector('.box');
	box.innerHTML = "Does this look good?<br>A: " + GlobalSetting_A + "<br>B: " + GlobalSetting_B + "<br>L: " + GlobalSetting_L + "<br>R: " + GlobalSetting_R + "<br>Up: " + GlobalSetting_Up + "<br>Right: " + GlobalSetting_Right + "<br>Down: " + GlobalSetting_Down + "<br>Left: " + GlobalSetting_Left + "<br><div class='centerDIV'><button class='button' onclick='SelectKeyboard()'>Yes</button> <button class='button' onclick='KBS1()'>No</button> <button class='button' onclick='ShowGlobalSettings()'>Cancel</button></div>";
}

function SelectController()
{
	if(GlobalTransientControllerValid)
	{
		GlobalTransientCheckControllerLoop = false;
		GlobalSetting_UseGamepad = true;
		StartDownload();
	}
}

function SelectKeyboard()
{
	GlobalSetting_UseGamepad = false;
	StartDownload();
}
	
// for if i decide to save these in cookies
function RemoveGlobalSettings()
{
	//try {
	//window.localStorage.removeItem('CGS_NAME');
	//window.localStorage.removeItem('CGS_GAMEPAD');
	//}
	//catch(e) {}
}
	
function CommitGlobalSettings()
{
	//GlobalSetting_Name = document.getElementById("NameTextbox").value;
	//try {
	//window.localStorage.setItem('CGS_NAME', GlobalSetting_Name);
	//window.localStorage.getItem('CGS_GAMEPAD', GlobalSetting_UseGamepad);
	//}
	//catch(e) {}
	//var box = document.querySelector('.box');
	//box.innerHTML = '<div class="spinner"></div>';
	//var script = document.createElement('script');
	//script.onerror = function() { var box = document.querySelector('.box'); box.innerHTML = 'There was an internal error running the core checks.<br>Please forward this message to the event coordinator.'; };
	//script.src = 'gamesettings.js';
	//document.body.appendChild(script);
}

function ControllerInputHelper(button, isDown)
{
	if(GlobalTransientControllerCache[button] != isDown)
	{
		_DoInput(button, isDown);
		GlobalTransientControllerCache[button] = isDown;
	}
}

function ControllerInputLoop()
{
	setTimeout(ControllerInputLoop, 15);
	const controllers = navigator.getGamepads();
	if(0 < controllers.length)
	{
		const c = controllers[0];
		if(c.mapping == "standard")
		{
			ControllerInputHelper(0, c.buttons[0].pressed);
			ControllerInputHelper(1, c.buttons[1].pressed);

			ControllerInputHelper(2, c.buttons[4].pressed || 0.7 < c.buttons[6].value);
			ControllerInputHelper(3, c.buttons[5].pressed || 0.7 < c.buttons[7].value);

			ControllerInputHelper(4, c.buttons[14].pressed || c.axes[0] < -0.7);
			ControllerInputHelper(5, c.buttons[15].pressed || 0.7 < c.axes[0]);
			ControllerInputHelper(6, c.buttons[12].pressed || c.axes[1] < -0.7);
			ControllerInputHelper(7, c.buttons[13].pressed || 0.7 < c.axes[1]);
		}
	}
}

function OnKeyDown(e)
{
	if(e.repeat) return;
	switch(e.code)
	{
	case GlobalSetting_A: _DoInput(0, true); return;
	case GlobalSetting_B: _DoInput(1, true); return;
	case GlobalSetting_L: _DoInput(2, true); return;
	case GlobalSetting_R: _DoInput(3, true); return;
	case GlobalSetting_Left: _DoInput(4, true); return;
	case GlobalSetting_Right: _DoInput(5, true); return;
	case GlobalSetting_Up: _DoInput(6, true); return;
	case GlobalSetting_Down: _DoInput(7, true); return;
	}

	// Debug Commands
	// Will have no effect if the shuffler was not compiled with debug enabled
	switch(e.code)
	{
	case "Digit1": _DebugSetGame(1); return;
	case "Digit2": _DebugSetGame(2); return;
	case "Digit3": _DebugSetGame(3); return;
	case "Digit4": _DebugSetGame(4); return;
	case "Digit5": _DebugSetGame(5); return;
	case "Digit6": _DebugEnableStep(); return;
	case "Digit7": _DebugDoStep(); return;
	case "Digit8": _DebugSave(); return;
	case "Digit9": _DebugStart(); return;
	}
}

function OnKeyUp(e)
{
	if(e.repeat) return;
	switch(e.code)
	{
	case GlobalSetting_A: _DoInput(0, false); break;
	case GlobalSetting_B: _DoInput(1, false); break;
	case GlobalSetting_L: _DoInput(2, false); break;
	case GlobalSetting_R: _DoInput(3, false); break;
	case GlobalSetting_Left: _DoInput(4, false); break;
	case GlobalSetting_Right: _DoInput(5, false); break;
	case GlobalSetting_Up: _DoInput(6, false); break;
	case GlobalSetting_Down: _DoInput(7, false); break;
	}
}
	
function StartDownload()
{
	var box = document.querySelector('.box');
	box.innerHTML = '<div class="spinner"></div><br><div class="progressContainer"><div class="progressBar" style="width: 0%;"></div></div>';

	const newScript = document.createElement('script');
	newScript.src = 'main.js';
	newScript.type = 'text/javascript';
	newScript.async = true; // Optional, for asynchronous loading
	document.body.appendChild(newScript);
}
	
function OnDownloadEnd()
{
	var box = document.querySelector('.box');
	box.style.width = '300px';
	box.style.background = 'black';
	box.innerHTML = '<div class="timer">--:--.--</div>';
	var foot = document.querySelector('.footer');
	foot.style.display = 'none';
	setTimeout(ShowGameBox, 100);
	var gameCanvas = document.querySelector('.webGLCanvas');
	gameCanvas.style.display = 'block';

	if(GlobalSetting_UseGamepad)
	{
		ControllerInputLoop();
	}
	else
	{
		addEventListener("keydown", OnKeyDown);
		addEventListener("keyup", OnKeyUp);
	}
}

// This is the object that connects my frontend to the emscripten code
// The functions need to be defined even if they do not do anything
var Module =
{
	print(...args)
	{
		console.log(...args);
	},
	canvas: document.querySelector('.webGLCanvas'),
	setStatus(text)
	{
		// Still not fully sure how this one works
		Module.setStatus.last ??= { time: Date.now(), text: '' };
		if (text === Module.setStatus.last.text) return;
		var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
		var now = Date.now();
		// if this is a progress update, skip it if too soon
		if (m && now - Module.setStatus.last.time < 30) return;
		Module.setStatus.last.time = now;
		Module.setStatus.last.text = text;
		if (m) {
			text = m[1];
			//var progressElement = document.querySelector('.DownloadBar');
			//progressElement.value = ((parseInt(m[2])*1.0)/parseInt(m[4]))*40;
			var progressElement = document.querySelector('.progressBar');
			progressElement.style.width = (((parseInt(m[2])*1.0)/parseInt(m[4]))*40) + "%";
			console.log("Progress " + (((parseInt(m[2])*1.0)/parseInt(m[4]))*40) + "%");
			//progressElement.max = parseInt(m[4])*100;
			// max should not be edited
		} else {
			// OnDownloadEnd();
			// Are we guarenteed this will always get hit?
			// have code call this instead
		}
	},
	totalDependencies: 0,
	monitorRunDependencies(left) {}
};
	
Step1();
