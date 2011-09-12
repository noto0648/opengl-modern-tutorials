#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
/* Using the GLUT library for the base windowing setup */
#include <GL/glut.h>
/* Using GLM for our transformation matrices */
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

GLuint program;
GLint attribute_coord2d;
GLint uniform_vertex_transform;
GLint uniform_texture_transform;
GLuint texture_id;
GLint uniform_mytexture;

float offset_x = 0.0;
float offset_y = 0.0;
float scale = 1.0;

bool interpolate = false;
bool clamp = false;
bool rotate = false;

GLuint vbo[2];

struct point {
  GLfloat x;
  GLfloat y;
};

/**
 * Store all the file's contents in memory, useful to pass shaders
 * source code to OpenGL
 */
char* file_read(const char* filename)
{
  FILE* in = fopen(filename, "rb");
  if (in == NULL) return NULL;

  int res_size = BUFSIZ;
  char* res = (char*)malloc(res_size);
  int nb_read_total = 0;

  while (!feof(in) && !ferror(in)) {
    if (nb_read_total + BUFSIZ > res_size) {
      if (res_size > 10*1024*1024) break;
      res_size = res_size * 2;
      res = (char*)realloc(res, res_size);
    }
    char* p_res = res + nb_read_total;
    nb_read_total += fread(p_res, 1, BUFSIZ, in);
  }
  
  fclose(in);
  res = (char*)realloc(res, nb_read_total + 1);
  res[nb_read_total] = '\0';
  return res;
}

/**
 * Display compilation errors from the OpenGL shader compiler
 */
void print_log(GLuint object)
{
  GLint log_length = 0;
  if (glIsShader(object))
    glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
  else if (glIsProgram(object))
    glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
  else {
    fprintf(stderr, "printlog: Not a shader or a program\n");
    return;
  }

  char* log = (char*)malloc(log_length);

  if (glIsShader(object))
    glGetShaderInfoLog(object, log_length, NULL, log);
  else if (glIsProgram(object))
    glGetProgramInfoLog(object, log_length, NULL, log);

  fprintf(stderr, "%s", log);
  free(log);
}

/**
 * Compile the shader from file 'filename', with error handling
 */
GLint create_shader(const char* filename, GLenum type)
{
  const GLchar* source = file_read(filename);
  if (source == NULL) {
    fprintf(stderr, "Error opening %s: ", filename); perror("");
    return 0;
  }
  GLuint res = glCreateShader(type);
  glShaderSource(res, 1, &source, NULL);
  free((void*)source);

  glCompileShader(res);
  GLint compile_ok = GL_FALSE;
  glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
  if (compile_ok == GL_FALSE) {
    fprintf(stderr, "%s:", filename);
    print_log(res);
    glDeleteShader(res);
    return 0;
  }

  return res;
}

int init_resources()
{
  GLint link_ok = GL_FALSE;

  GLuint vs, fs;
  if ((vs = create_shader("graph.v.glsl", GL_VERTEX_SHADER))   == 0) return 0;
  if ((fs = create_shader("graph.f.glsl", GL_FRAGMENT_SHADER)) == 0) return 0;

  program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &link_ok);
  if (!link_ok) {
    fprintf(stderr, "glLinkProgram:");
    print_log(program);
    return 0;
  }

  const char* attribute_name;
  attribute_name = "coord2d";
  attribute_coord2d = glGetAttribLocation(program, attribute_name);
  if (attribute_coord2d == -1) {
    fprintf(stderr, "Could not bind attribute %s\n", attribute_name);
    return 0;
  }

  const char* uniform_name;
  uniform_name = "vertex_transform";
  uniform_vertex_transform = glGetUniformLocation(program, uniform_name);
  if (uniform_vertex_transform == -1) {
    fprintf(stderr, "Could not bind uniform %s\n", uniform_name);
    return 0;
  }

  uniform_name = "texture_transform";
  uniform_texture_transform = glGetUniformLocation(program, uniform_name);
  if (uniform_texture_transform == -1) {
    fprintf(stderr, "Could not bind uniform %s\n", uniform_name);
    return 0;
  }

  // Create our datapoints, store it as bytes
  #define N 256
  GLbyte graph[N][N];

  for(int i = 0; i < N; i++) {
    for(int j = 0; j < N; j++) {
      float x = (i - N / 2) / (N / 2.0);
      float y = (j - N / 2) / (N / 2.0);
      float d = hypotf(x, y) * 4.0;
      float z = (1 - d * d) * expf(d * d / -2.0);
      graph[i][j] = roundf(z * 127 + 128);
    }
  }

  /* Upload the texture with our datapoints */
  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glTexImage2D(
     GL_TEXTURE_2D,      // target
     0,                  // level, 0 = base, no minimap,
     GL_LUMINANCE,       // internalformat
     N,                  // width
     N,                  // height
     0,                  // border, always 0 in OpenGL ES
     GL_LUMINANCE,       // format
     GL_UNSIGNED_BYTE,   // type
     graph
  );

  // Create two vertex buffer objects
  glGenBuffers(2, vbo);

  // Create an array for 101 * 101 vertices
  point vertices[101][101];

  for(int i = 0; i < 101; i++) {
    for(int j = 0; j < 101; j++) {
      vertices[i][j].x = (j - 50) / 50.0;
      vertices[i][j].y = (i - 50) / 50.0;
    }
  }

  // Tell OpenGL to copy our array to the buffer objects
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);

  // Create an array of indices into the vertex array that traces both horizontal and vertical lines
  GLushort indices[100 * 101 * 4];
  int i = 0;

  for(int y = 0; y < 101; y++) {
    for(int x = 0; x < 100; x++) {
      indices[i++] = y * 101 + x;
      indices[i++] = y * 101 + x + 1;
    }
  }

  for(int x = 0; x < 101; x++) {
    for(int y = 0; y < 100; y++) {
      indices[i++] = y * 101 + x;
      indices[i++] = (y + 1) * 101 + x;
    }
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[1]);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);

  return 1;
}

void display()
{
  glUseProgram(program);
  glUniform1i(uniform_mytexture, 0);

  glm::mat4 model;

  if(rotate)
    model = glm::rotate(glm::mat4(1.0f), float(glutGet(GLUT_ELAPSED_TIME) / 100.0), glm::vec3(0.0f, 0.0f, 1.0f));
  else
    model = glm::mat4(1.0f);  

  glm::mat4 view = glm::lookAt(glm::vec3(0.0, -2.0, 2.0), glm::vec3(0.0, 0.0, 0.0), glm::vec3(0.0, 0.0, 1.0));
  glm::mat4 projection = glm::perspective(45.0f, 1.0f*640/480, 0.1f, 10.0f);

  glm::mat4 vertex_transform = projection * view * model * glm::mat4(1.0f);
  glm::mat4 texture_transform = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(scale, scale, 1)), glm::vec3(offset_x, offset_y, 0));

  glUniformMatrix4fv(uniform_vertex_transform, 1, GL_FALSE, glm::value_ptr(vertex_transform));
  glUniformMatrix4fv(uniform_texture_transform, 1, GL_FALSE, glm::value_ptr(texture_transform));

  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT);

  /* Set texture wrapping mode */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);

  /* Set texture interpolation mode */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, interpolate ? GL_LINEAR : GL_NEAREST);

  /* Draw the grid using the indices to our vertices using our vertex buffer objects */
  glEnableVertexAttribArray(attribute_coord2d);

  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glVertexAttribPointer(attribute_coord2d, 2, GL_FLOAT, GL_FALSE, 0, 0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[1]);
  glDrawElements(GL_LINES, 100 * 101 * 4, GL_UNSIGNED_SHORT, 0);

  /* Stop using the vertex buffer object */
  glDisableVertexAttribArray(attribute_coord2d);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  glutSwapBuffers();
}

void special(int key, int x, int y)
{
  switch(key) {
    case GLUT_KEY_F1:
      interpolate = !interpolate;
      break;
    case GLUT_KEY_F2:
      clamp = !clamp;
      break;
    case GLUT_KEY_F3:
      rotate = !rotate;
      break;
    case GLUT_KEY_LEFT:
      offset_x -= 0.03;
      break;
    case GLUT_KEY_RIGHT:
      offset_x += 0.03;
      break;
    case GLUT_KEY_UP:
      offset_y += 0.03;
      break;
    case GLUT_KEY_DOWN:
      offset_y -= 0.03;
      break;
    case GLUT_KEY_PAGE_UP:
      scale *= 1.5;
      break;
    case GLUT_KEY_PAGE_DOWN:
      scale /= 1.5;
      break;
    case GLUT_KEY_HOME:
      offset_x = 0.0;
      offset_y = 0.0;
      scale = 1.0;
      break;
  }

  glutPostRedisplay();
}

void free_resources()
{
  glDeleteProgram(program);
}

int main(int argc, char* argv[]) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA|GLUT_DOUBLE);
  glutInitWindowSize(640, 480);
  glutCreateWindow("My Graph");

  if (init_resources()) {
    glutDisplayFunc(display);
    glutIdleFunc(display);
    glutSpecialFunc(special);
    glutMainLoop();
  }

  free_resources();
  return 0;
}
