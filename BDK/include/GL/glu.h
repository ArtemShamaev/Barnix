#ifndef __glu_h__
#define __glu_h__
#include <GL/gl.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Barnix GLU: camera helpers, projection, mipmaps and quadrics. */
#define GLU_FALSE 0
#define GLU_TRUE 1
#define GLU_INVALID_ENUM 100900
#define GLU_INVALID_VALUE 100901
#define GLU_OUT_OF_MEMORY 100902
#define GLU_VERSION 100800
#define GLU_EXTENSIONS 100801

/* Quadric draw styles, normals and orientation */
#define GLU_POINT 100010
#define GLU_LINE 100011
#define GLU_FILL 100012
#define GLU_SILHOUETTE 100013
#define GLU_SMOOTH 100000
#define GLU_FLAT 100001
#define GLU_NONE 100002
#define GLU_OUTSIDE 100020
#define GLU_INSIDE 100021

typedef struct GLUquadric GLUquadric;
typedef GLUquadric GLUquadricObj;

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble z_near, GLdouble z_far);
void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
void gluLookAt(GLdouble eye_x, GLdouble eye_y, GLdouble eye_z, GLdouble center_x, GLdouble center_y,
               GLdouble center_z, GLdouble up_x, GLdouble up_y, GLdouble up_z);
void gluPickMatrix(GLdouble x, GLdouble y, GLdouble width, GLdouble height, const GLint viewport[4]);
GLint gluProject(GLdouble obj_x, GLdouble obj_y, GLdouble obj_z, const GLdouble model[16], const GLdouble proj[16],
                 const GLint viewport[4], GLdouble *win_x, GLdouble *win_y, GLdouble *win_z);
GLint gluUnProject(GLdouble win_x, GLdouble win_y, GLdouble win_z, const GLdouble model[16], const GLdouble proj[16],
                   const GLint viewport[4], GLdouble *obj_x, GLdouble *obj_y, GLdouble *obj_z);
const GLubyte *gluErrorString(GLenum error);
const GLubyte *gluGetString(GLenum name);

GLint gluScaleImage(GLenum format, GLsizei width_in, GLsizei height_in, GLenum type_in, const void *data_in,
                    GLsizei width_out, GLsizei height_out, GLenum type_out, GLvoid *data_out);
GLint gluBuild2DMipmaps(GLenum target, GLint internal_format, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const void *data);

GLUquadric *gluNewQuadric(void);
void gluDeleteQuadric(GLUquadric *quad);
void gluQuadricDrawStyle(GLUquadric *quad, GLenum draw);
void gluQuadricNormals(GLUquadric *quad, GLenum normal);
void gluQuadricOrientation(GLUquadric *quad, GLenum orientation);
void gluQuadricTexture(GLUquadric *quad, GLboolean texture);
void gluSphere(GLUquadric *quad, GLdouble radius, GLint slices, GLint stacks);
void gluCylinder(GLUquadric *quad, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks);
void gluDisk(GLUquadric *quad, GLdouble inner, GLdouble outer, GLint slices, GLint loops);
void gluPartialDisk(GLUquadric *quad, GLdouble inner, GLdouble outer, GLint slices, GLint loops,
                    GLdouble start, GLdouble sweep);
#ifdef __cplusplus
}
#endif
#endif
