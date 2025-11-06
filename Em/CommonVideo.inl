namespace CommonVideo
{
	bool renderStuffSet = false;
	GLuint GLProgram = 0;
	GLuint GLLocationTex = 0;
	GLuint GLLocationScale = 0;
	GLuint GLLocationPos = 0;
	GLuint GLLocationUV = 0;
	GLuint GLBuffer = 0;
	GLuint GLIndexBuffer = 0;
	GLuint GLVertexShader = 0;
	GLuint GLPixelShader = 0;

	void InitRenderStuff(void)
	{
		if(!renderStuffSet)
		{
			char const *vertexText = 
				"attribute vec2 pos;\n"
				"attribute vec2 uv;\n"
				"uniform vec2 scale;\n"
				"varying vec2 puv;\n"
				"void main(void) { gl_Position = vec4(pos, 0, 1); puv = uv * scale; }";
			char const *pixelText =
				"#ifdef GL_ES\n"
				"precision mediump float;\n"
				"#endif\n"
				"varying vec2 puv;\n"
				"uniform sampler2D tex;\n"
				"void main(void) { gl_FragColor = texture2D(tex, puv); }";

			float const fsQuad[] = {
				-1, 1, 0, 0,
				-1, -1, 0, 1,
				1, -1, 1, 1,
				1, 1, 1, 0};
			unsigned char const fsQuadIndex[] = {
				0, 1, 2,
				2, 3, 0};

			GLVertexShader = glCreateShader(GL_VERTEX_SHADER);
			glShaderSource(GLVertexShader, 1, &vertexText, NULL);
			glCompileShader(GLVertexShader);

			GLPixelShader = glCreateShader(GL_FRAGMENT_SHADER);
			glShaderSource(GLPixelShader, 1, &pixelText, NULL);
			glCompileShader(GLPixelShader);

			GLProgram = glCreateProgram();
			glAttachShader(GLProgram, GLVertexShader);
			glAttachShader(GLProgram, GLPixelShader);
			glLinkProgram(GLProgram);

			GLLocationTex = glGetUniformLocation(GLProgram, "tex");
			GLLocationScale = glGetUniformLocation(GLProgram, "scale");
			GLLocationPos = glGetAttribLocation(GLProgram, "pos");
			GLLocationUV = glGetAttribLocation(GLProgram, "uv");

			glGenBuffers(1, &GLBuffer);
			glBindBuffer(GL_ARRAY_BUFFER, GLBuffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(fsQuad), fsQuad, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glGenBuffers(1, &GLIndexBuffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GLIndexBuffer);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(fsQuadIndex), fsQuadIndex, GL_STATIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

			renderStuffSet = true;
		}
	}

	GLuint GetTexture(int size)
	{
		// init texture to black
		GLuint GLTexture = 0;
		glGenTextures(1, &GLTexture);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, GLTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, 
			GL_RGBA, 
			size, size, 0, 
			GL_RGBA, 
			GL_UNSIGNED_BYTE, 
			nullptr);
		glBindTexture(GL_TEXTURE_2D, 0);
		return GLTexture;
	}

	void DrawRenderStuff(GLuint GLTexture, int textureSize, uint8_t screen[], int screenWidth, int screenHeight)
	{
		if(renderStuffSet)
		{
			glClearColor(0.0, 0.0, 0.0, 0.0);
			glClear(GL_COLOR_BUFFER_BIT);

			glUseProgram(GLProgram);
			glDisable(GL_BLEND);
			glDisable(GL_DEPTH_TEST);
			glFrontFace(GL_CCW);
			glDisable(GL_CULL_FACE);

			int x = 0, y = 0, width = GetGameCanvasWidth(), height = GetGameCanvasHeight();
			if(float(screenWidth)/screenHeight < float(width)/height)
			{
				float widthScale = ((height/float(screenHeight)) * float(screenWidth))/width;
				x = (width - (width * widthScale)) / 2;
				width *= widthScale;
			}
			else
			{
				float heightScale = ((width/float(screenWidth)) * float(screenHeight))/height;
				y = (height - (height * heightScale)) / 2;
				height *= heightScale;
			}
			glViewport(x, y, width, height);

			glBindBuffer(GL_ARRAY_BUFFER, GLBuffer);
			glVertexAttribPointer(GLLocationPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
			glEnableVertexAttribArray(GLLocationPos);
			glVertexAttribPointer(GLLocationUV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (char*)(nullptr) + (2 * sizeof(float)));
			glEnableVertexAttribArray(GLLocationUV);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, GLTexture);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 
				0, 0, 
				screenWidth, screenHeight, 
				GL_RGBA, 
				GL_UNSIGNED_BYTE, 
				screen);
			glUniform1i(GLLocationTex, 0);
			glUniform2f(GLLocationScale, float(screenWidth) / textureSize, float(screenHeight) / textureSize);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GLIndexBuffer);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, nullptr);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

			glBindTexture(GL_TEXTURE_2D, 0);
			glDisableVertexAttribArray(GLLocationUV);
			glDisableVertexAttribArray(GLLocationPos);
			glUseProgram(0);

			glutSwapBuffers();
		}
	}
}
