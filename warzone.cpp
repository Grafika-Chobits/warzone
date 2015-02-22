#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

/* SETTINGS ------------------------------------------------------------ */
#define screenXstart 250
#define screenX 1366
#define screenY 768
#define mouseSensitivity 1

#define min(X,Y) (((X) < (Y)) ? (X) : (Y))
#define max(X,Y) (((X) > (Y)) ? (X) : (Y))

#define PI 3.14159265

using namespace std;

/* TYPEDEFS ------------------------------------------------------------ */

//RGB color
typedef struct s_rgb {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} RGB;

//Frame of RGBs
typedef struct s_frame {
	RGB px[screenX][screenY];
} Frame;

//Coordinate System
typedef struct s_coord {
	int x;
	int y;
} Coord;

//The integrated frame buffer plus info struct.
typedef struct s_frameBuffer {
	char* ptr;
	int smemLen;
	int lineLen;
	int bpp;
} FrameBuffer;



/* MATH STUFF ---------------------------------------------------------- */

// construct coord
Coord coord(int x, int y) {
	Coord retval;
	retval.x = x;
	retval.y = y;
	return retval;
}

unsigned char isInBound(Coord position, Coord corner1, Coord corner2) {
	unsigned char xInBound = 0;
	unsigned char yInBound = 0;
	if (corner1.x < corner2.x) {
		xInBound = (position.x>corner1.x) && (position.x<corner2.x);
	} else if (corner1.x > corner2.x) {
		xInBound = (position.x>corner2.x) && (position.x<corner1.x);
	} else {
		return 0;
	}
	if (corner1.y < corner2.y) {
		yInBound = (position.y>corner1.y) && (position.y<corner2.y);
	} else if (corner1.y > corner2.y) {
		yInBound = (position.y>corner2.y) && (position.y<corner1.y);
	} else {
		return 0;
	}
	return xInBound&&yInBound;
}

/* MOUSE OPERATIONS ---------------------------------------------------- */

// get mouse coord, with integrated screen-space bounding
Coord getCursorCoord(Coord* mc) {
	Coord xy;
	if (mc->x < 0) {
		mc->x = 0;
		xy.x = 0;
	} else if (mc->x >= screenX*mouseSensitivity) {
		mc->x = screenX*mouseSensitivity-1;
		xy.x = screenX-1;
	} else {
		xy.x = (int) mc->x / mouseSensitivity;
	}
	if (mc->y < 0) {
		mc->y = 0;
		xy.y = 0;
	} else if (mc->y >= screenY*mouseSensitivity) {
		mc->y = screenY*mouseSensitivity-1;
		xy.y = screenY-1;
	} else {
		xy.y = (int) mc->y / mouseSensitivity;
	}
	return xy;
}

/* VIDEO OPERATIONS ---------------------------------------------------- */

// construct RGB
RGB rgb(unsigned char r, unsigned char g, unsigned char b) {
	RGB retval;
	retval.r = r;
	retval.g = g;
	retval.b = b;
	return retval;
}

// insert pixel to composition frame, with bounds filter
void insertPixel(Frame* frm, Coord loc, RGB col) {
	// do bounding check:
	if (!(loc.x >= screenX || loc.x < 0 || loc.y >= screenY || loc.y < 0)) {
		frm->px[loc.x][loc.y].r = col.r;
		frm->px[loc.x][loc.y].g = col.g;
		frm->px[loc.x][loc.y].b = col.b;
	}
}

// delete contents of composition frame
void flushFrame (Frame* frm, RGB color) {
	int x;
	int y;
	for (y=0; y<screenY; y++) {
		for (x=0; x<screenX; x++) {
			frm->px[x][y] = color;
		}
	}
}

// copy composition Frame to FrameBuffer
void showFrame (Frame* frm, FrameBuffer* fb) {
	int x;
	int y;
	for (y=0; y<screenY; y++) {
		for (x=0; x<screenX; x++) {
			int location = x * (fb->bpp/8) + y * fb->lineLen;
			*(fb->ptr + location    ) = frm->px[x][y].b; // blue
			*(fb->ptr + location + 1) = frm->px[x][y].g; // green
			*(fb->ptr + location + 2) = frm->px[x][y].r; // red
			*(fb->ptr + location + 3) = 255; // transparency
		}
	}
}

void showCanvas(Frame* frm, Frame* cnvs, int canvasWidth, int canvasHeight, Coord loc, RGB borderColor, int isBorder) {
	int x, y;
	for (y=0; y<canvasHeight;y++) {
		for (x=0; x<canvasWidth; x++) {
			insertPixel(frm, coord(loc.x - canvasWidth/2 + x, loc.y - canvasHeight/2 + y), cnvs->px[x][y]);
		}
	}
	
	//show border
	if(isBorder){
		for (y=0; y<canvasHeight; y++) {
			insertPixel(frm, coord(loc.x - canvasWidth/2 - 1, loc.y - canvasHeight/2 + y), borderColor);
			insertPixel(frm, coord(loc.x  - canvasWidth/2 + canvasWidth, loc.y - canvasHeight/2 + y), borderColor);
		}
		for (x=0; x<canvasWidth; x++) {
			insertPixel(frm, coord(loc.x - canvasWidth/2 + x, loc.y - canvasHeight/2 - 1), borderColor);
			insertPixel(frm, coord(loc.x - canvasWidth/2 + x, loc.y - canvasHeight/2 + canvasHeight), borderColor);
		}
	}
}

void plotCircle(Frame* frm,int xm, int ym, int r,RGB col)
{
   int x = -r, y = 0, err = 2-2*r; /* II. Quadrant */ 
   do {
      insertPixel(frm,coord(xm-x, ym+y),col); /*   I. Quadrant */
      insertPixel(frm,coord(xm-y, ym-x),col); /*  II. Quadrant */
      insertPixel(frm,coord(xm+x, ym-y),col); /* III. Quadrant */
      insertPixel(frm,coord(xm+y, ym+x),col); /*  IV. Quadrant */
      r = err;
      if (r <= y) err += ++y*2+1;           /* e_xy+e_y < 0 */
      if (r > x || err > y) err += ++x*2+1; /* e_xy+e_x > 0 or no 2nd y-step */
   } while (x < 0);
}

void plotHalfCircle(Frame *frm,int xm, int ym, int r,RGB col)
{
   int x = -r, y = 0, err = 2-2*r; /* II. Quadrant */ 
   do {
      insertPixel(frm,coord(xm+x, ym-y),col); /* III. Quadrant */
      insertPixel(frm,coord(xm+y, ym+x),col); /*  IV. Quadrant */
      r = err;
      if (r <= y) err += ++y*2+1;           /* e_xy+e_y < 0 */
      if (r > x || err > y) err += ++x*2+1; /* e_xy+e_x > 0 or no 2nd y-step */
   } while (x < 0);
}

/* Fungsi membuat garis */
void plotLine(Frame* frm, int x0, int y0, int x1, int y1, RGB lineColor)
{
	int dx =  abs(x1-x0), sx = x0<x1 ? 1 : -1;
	int dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1; 
	int err = dx+dy, e2; /* error value e_xy */
	int loop = 1;
	while(loop){  /* loop */
		insertPixel(frm, coord(x0, y0), rgb(lineColor.r, lineColor.g, lineColor.b));
		if (x0==x1 && y0==y1) loop = 0;
		e2 = 2*err;
		if (e2 >= dy) { err += dy; x0 += sx; } /* e_xy+e_x > 0 */
		if (e2 <= dx) { err += dx; y0 += sy; } /* e_xy+e_y < 0 */
	}
}

/* FUNCTIONS FOR SCANLINE ALGORITHM ---------------------------------------------------- */

bool isSlopeEqualsZero(int y0, int y1){
	if(y0 == y1){
		return true;
	}else{
		return false;
	}
}

bool isInBetween(int y0, int y1, int yTest){
	if((yTest >= y0 && yTest <= y1 || yTest >= y1 && yTest <= y0) && !isSlopeEqualsZero(y0, y1)){
		return true;
	}else{
		return false;
	}
}

/* Function to calculate intersection between line (a,b) and line with slope 0 */
Coord intersection(Coord a, Coord b, int y){
	int x;
	double slope;
	
	if(b.x == a.x){
		x = a.x;
	}else{
		slope = (double)(b.y - a.y) / (double)(b.x - a.x);
		x = round(((double)(y - a.y) / slope) + (double)a.x);
	}
	
	return coord(x, y);
}

bool compareByAxis(const s_coord &a, const s_coord &b){
	return a.x <= b.x;
}

bool compareSameAxis(const s_coord &a, const s_coord &b){
	return a.x == b.x;
}

vector<Coord> intersectionGenerator(int y, vector<Coord> polygon){
	vector<Coord> intersectionPoint;
	
	for(int i = 0; i < polygon.size(); i++){
		if(i == polygon.size() - 1){
			if(isInBetween(polygon.at(i).y, polygon.at(0).y, y)){				
				Coord a = coord(polygon.at(i).x, polygon.at(i).y);
				Coord b = coord(polygon.at(0).x, polygon.at(0).y);
				
				intersectionPoint.push_back(intersection(a, b, y));
			}
		}else{
			if(isInBetween(polygon.at(i).y, polygon.at(i + 1).y, y)){
				Coord a = coord(polygon.at(i).x, polygon.at(i).y);
				Coord b = coord(polygon.at(i + 1).x, polygon.at(i + 1).y);
				
				intersectionPoint.push_back(intersection(a, b, y));
			}
		}
	}
	
	sort(intersectionPoint.begin(), intersectionPoint.end(), compareByAxis);
	
	return intersectionPoint;
}

vector<Coord> combineIntersection(vector<Coord> a, vector<Coord> b){
	for(int i = 0; i < b.size(); i++){
		a.push_back(b.at(i));
	}
	
	sort(a.begin(), a.end(), compareByAxis);
	
	return a;
}

void drawAmmunition(Frame *frame, Coord upperBoundPosition, int ammunitionWidth, int ammunitionLength, RGB color){
	plotLine(frame, upperBoundPosition.x, upperBoundPosition.y, upperBoundPosition.x, upperBoundPosition.y + ammunitionLength, color);
	
	int i;
	for(i = 0; i < ammunitionWidth; i++){
		plotLine(frame, upperBoundPosition.x + i, upperBoundPosition.y, upperBoundPosition.x + i, upperBoundPosition.y + ammunitionLength, color);
		plotLine(frame, upperBoundPosition.x - i, upperBoundPosition.y, upperBoundPosition.x - i, upperBoundPosition.y + ammunitionLength, color);
	}	
}

void drawPeluru(Frame *frame, Coord center, RGB color)
{
	int panjangPeluru = 10;
	//DrawKiri
	plotLine(frame, center.x - 3, center.y + panjangPeluru / 2, center.x -3, center.y - panjangPeluru / 2, color); 
	
	//DrawKanan
	plotLine(frame, center.x + 3, center.y + panjangPeluru / 2, center.x + 3, center.y - panjangPeluru / 2, color);
	
	//DrawBawah
	plotLine(frame, center.x - 3, center.y + panjangPeluru / 2, center.x +3, center.y + panjangPeluru / 2, color);
	
	//DrawUjungKiri
	plotLine(frame, center.x - 3, center.y - panjangPeluru / 2, center.x, center.y - (panjangPeluru / 2 + 4), color);
	
	//DrawUjungKanan
	plotLine(frame, center.x + 3, center.y - panjangPeluru / 2, center.x, center.y - (panjangPeluru / 2 + 4), color);
}

void drawPlane(Frame *frame, Coord position, RGB color) {

	// Ship's relative coordinate to canvas, ship's actuator
	int xPlaneCoordinate = position.x;
	int yPlaneCoordinate = position.y;
	
	// Ship's border coordinates
	vector<Coord>  planeCoordinates;
	planeCoordinates.push_back(coord(0,0));
	planeCoordinates.push_back(coord(planeCoordinates.at(0).x + 15, planeCoordinates.at(0).y-5));
	planeCoordinates.push_back(coord(planeCoordinates.at(1).x + 30, planeCoordinates.at(1).y-3));
	planeCoordinates.push_back(coord(planeCoordinates.at(2).x + 13, planeCoordinates.at(2).y-4));
	planeCoordinates.push_back(coord(planeCoordinates.at(3).x + 13, planeCoordinates.at(3).y-3));
	planeCoordinates.push_back(coord(planeCoordinates.at(4).x + 13, planeCoordinates.at(4).y+5));
	planeCoordinates.push_back(coord(planeCoordinates.at(5).x + 13, planeCoordinates.at(5).y+4));
	planeCoordinates.push_back(coord(planeCoordinates.at(6).x + 50, planeCoordinates.at(6).y-3));
	planeCoordinates.push_back(coord(planeCoordinates.at(7).x + 5, planeCoordinates.at(7).y-18));
	planeCoordinates.push_back(coord(planeCoordinates.at(8).x + 10, planeCoordinates.at(8).y-4));
	planeCoordinates.push_back(coord(planeCoordinates.at(9).x + 3, planeCoordinates.at(9).y+27));
	planeCoordinates.push_back(coord(planeCoordinates.at(10).x - 1, planeCoordinates.at(10).y+5));
	planeCoordinates.push_back(coord(planeCoordinates.at(11).x + 1, planeCoordinates.at(11).y+5));
	planeCoordinates.push_back(coord(planeCoordinates.at(12).x - 69, planeCoordinates.at(12).y+4));
	planeCoordinates.push_back(coord(planeCoordinates.at(13).x + 13, planeCoordinates.at(13).y+25));
	planeCoordinates.push_back(coord(planeCoordinates.at(14).x - 10, planeCoordinates.at(14).y-6));
	planeCoordinates.push_back(coord(planeCoordinates.at(15).x - 17, planeCoordinates.at(15).y-18));
	planeCoordinates.push_back(coord(planeCoordinates.at(16).x - 37, planeCoordinates.at(16).y-2));
	planeCoordinates.push_back(coord(planeCoordinates.at(17).x - 27, planeCoordinates.at(17).y-4));
	

	// Draw ship's border relative to canvas
	for(int i = 0; i < planeCoordinates.size(); i++){
		int x0, y0, x1, y1;
		
		if(i < planeCoordinates.size() - 1){
			x0 = planeCoordinates.at(i).x + xPlaneCoordinate;
			y0 = planeCoordinates.at(i).y + yPlaneCoordinate;
			x1 = planeCoordinates.at(i + 1).x + xPlaneCoordinate;
			y1 = planeCoordinates.at(i + 1).y + yPlaneCoordinate;
		}else{
			x0 = planeCoordinates.at(planeCoordinates.size() - 1).x + xPlaneCoordinate;
			y0 = planeCoordinates.at(planeCoordinates.size() - 1).y + yPlaneCoordinate;
			x1 = planeCoordinates.at(0).x + xPlaneCoordinate;
			y1 = planeCoordinates.at(0).y + yPlaneCoordinate;
		}
		
		plotLine(frame, x0, y0, x1, y1, color);
	}

	// Coloring ship using scanline algorithm
	int height = 65;
	for(int i = -31; i <= height-31; i++){
		vector<Coord> planeIntersectionPoint = intersectionGenerator(i, planeCoordinates);
		
		if(planeIntersectionPoint.size() % 2 != 0){
			unique(planeIntersectionPoint.begin(), planeIntersectionPoint.end(), compareSameAxis);
			planeIntersectionPoint.erase(planeIntersectionPoint.end() - 1);
		}
		
		for(int j = 0; j < planeIntersectionPoint.size() - 1; j++){
			if(j % 2 == 0){
				int x0 = planeIntersectionPoint.at(j).x + xPlaneCoordinate;
				int y0 = planeIntersectionPoint.at(j).y + yPlaneCoordinate;
				int x1 = planeIntersectionPoint.at(j + 1).x + xPlaneCoordinate;
				int y1 = planeIntersectionPoint.at(j + 1).y + yPlaneCoordinate;
				
				plotLine(frame, x0, y0, x1, y1, color);
			}
		}		
	}
	
}

void drawExplosion(Frame *frame, Coord loc, int mult, RGB color){	
	plotLine(frame,loc.x+10*mult,loc.y +10*mult,loc.x+20*mult,loc.y+20*mult,color);
	plotLine(frame,loc.x-10*mult,loc.y -10*mult,loc.x-20*mult,loc.y-20*mult,color);
	plotLine(frame,loc.x+10*mult,loc.y -10*mult,loc.x+20*mult,loc.y-20*mult,color);
	plotLine(frame,loc.x-10*mult,loc.y +10*mult,loc.x-20*mult,loc.y+20*mult,color);
	plotLine(frame,loc.x,loc.y -10*mult,loc.x,loc.y-20*mult,color);
	plotLine(frame,loc.x-10*mult,loc.y,loc.x-20*mult,loc.y,color);
	plotLine(frame,loc.x+10*mult,loc.y ,loc.x+20*mult,loc.y,color);
	plotLine(frame,loc.x,loc.y +10*mult,loc.x,loc.y+20*mult,color);
}

void animateExplosion(Frame* frame, int explosionMul, Coord loc){
	int explosionR, explosionG, explosionB;
	explosionR = explosionG = explosionB = 255-explosionMul*12;	
	if(explosionR <= 0 || explosionG <= 0 || explosionB <= 0){
		explosionR = explosionG = explosionB = 0;
	}
	drawExplosion(frame, loc, explosionMul, rgb(explosionR, 0, 0));
}

void drawBomb(Frame *frame, Coord center, RGB color)
{
	int panjangBomb = 10;
	//DrawKiri
	plotLine(frame, center.x - 3, center.y + panjangBomb / 2, center.x -3, center.y - panjangBomb / 2, color); 
	
	//DrawKanan
	plotLine(frame, center.x + 3, center.y + panjangBomb / 2, center.x + 3, center.y - panjangBomb / 2, color);
	
	//DrawAtas
	plotLine(frame, center.x - 3, center.y - panjangBomb / 2, center.x +3, center.y - panjangBomb / 2, color);
	
	//DrawUjungKiri
	plotLine(frame, center.x - 3, center.y + panjangBomb / 2, center.x, center.y + (panjangBomb / 2 + 4), color);
	
	//DrawUjungKanan
	plotLine(frame, center.x + 3, center.y + panjangBomb / 2, center.x, center.y + (panjangBomb / 2 + 4), color);
}

void drawParachute(Frame *frame, Coord center, RGB color, int size){
	int parachuteRadius = size;
	int parachuteDiameter = parachuteRadius * 2;
	
	// parachute upper border
	plotHalfCircle(frame, center.x, center.y, parachuteRadius, color);
	
	// parachute bottom border
	plotHalfCircle(frame, center.x - parachuteDiameter / 3, center.y, parachuteRadius / 3, color);
	plotHalfCircle(frame, center.x, center.y, parachuteRadius / 3, color);
	plotHalfCircle(frame, center.x + parachuteDiameter / 3, center.y, parachuteRadius / 3, color);
	
	// parachute string
	plotLine(frame, center.x - parachuteRadius, center.y, center.x - parachuteRadius / 6, center.y + parachuteRadius, color); // left
	plotLine(frame, center.x + parachuteRadius, center.y, center.x + parachuteRadius / 6, center.y + parachuteRadius, color); // right
	
	// stickman
	plotCircle(frame, center.x, center.y + parachuteRadius - parachuteRadius / 12, parachuteRadius / 12, color); //head
	
	int bodyStartingPoint = center.y + parachuteRadius - parachuteRadius / 10 + parachuteRadius / 10;
	plotLine(frame, center.x, bodyStartingPoint, center.x, bodyStartingPoint + parachuteRadius / 5, color); // body
	
	int legStartingPoint = bodyStartingPoint + parachuteRadius / 5;
	plotLine(frame, center.x, legStartingPoint, center.x + parachuteRadius / 13, legStartingPoint + parachuteRadius / 10, color); // right leg
	plotLine(frame, center.x, legStartingPoint, center.x - parachuteRadius / 13, legStartingPoint + parachuteRadius / 10, color); // left leg
	
	plotLine(frame, center.x, bodyStartingPoint, center.x - parachuteRadius / 10, bodyStartingPoint + parachuteRadius / 10, color);
	plotLine(frame, center.x, bodyStartingPoint, center.x + parachuteRadius / 10, bodyStartingPoint + parachuteRadius / 10, color);
	plotLine(frame, center.x - parachuteRadius / 10, bodyStartingPoint + parachuteRadius / 10, center.x - parachuteRadius / 6, center.y + parachuteRadius, color);
	plotLine(frame, center.x + parachuteRadius / 10, bodyStartingPoint + parachuteRadius / 10, center.x + parachuteRadius / 6, center.y + parachuteRadius, color);
}

Coord lengthEndPoint(Coord startingPoint, int degree, int length){
	Coord endPoint;
	
	endPoint.x = int((double)length * cos((double)degree * PI / (double)180)) + startingPoint.x;
	endPoint.y = int((double)length * sin((double)degree * PI / (double)180)) + startingPoint.y;
	
	return endPoint;
}

void drawWalkingStickman(Frame *frame, Coord center, RGB color){
	int bodyLength = 50;
	int rightUpperArmLength = 30;
	int rightLowerArmLength = 20;
	int leftUpperArmLength = 30;
	int leftLowerArmLength = 20;
	int rightUpperLegLength = 30;
	int rightLowerLegLength = 20;
	int leftUpperLegLength = 30;
	int leftLowerLegLength = 20;
	
	static int centerPositionY = center.y;
	
	// head
	plotCircle(frame, center.x, centerPositionY - 20, 20, color);
	
	// body	
	Coord bodyEndPoint = lengthEndPoint(coord(center.x, centerPositionY), 88, bodyLength);
	plotLine(frame, center.x, centerPositionY, bodyEndPoint.x, bodyEndPoint.y, color);
	
	// right upper arm
	Coord rightUpperArmEndPoint;
	static int rightUpperArmRotation = 125;
	{
		static int moveBackwardArm = 1;
		
		if(rightUpperArmRotation == 125){
			moveBackwardArm = 1;
		}
		if(rightUpperArmRotation == 65){
			moveBackwardArm = 0;
		}
		
		
		if(moveBackwardArm){
			rightUpperArmRotation -= 5;
		}else{
			rightUpperArmRotation += 5;
		}
		
		rightUpperArmEndPoint = lengthEndPoint(coord(center.x, centerPositionY), rightUpperArmRotation, rightUpperArmLength);
		plotLine(frame, center.x, centerPositionY, rightUpperArmEndPoint.x, rightUpperArmEndPoint.y, color);
	}
	
	// right lower arm
	Coord rightLowerArmEndPoint = lengthEndPoint(coord(rightUpperArmEndPoint.x, rightUpperArmEndPoint.y), rightUpperArmRotation + 50, rightLowerArmLength);
	plotLine(frame, rightUpperArmEndPoint.x, rightUpperArmEndPoint.y, rightLowerArmEndPoint.x, rightLowerArmEndPoint.y, color);
	
	// left upper arm
	Coord leftUpperArmEndPoint;
	static int leftUpperArmRotation = 65;
	{
		
		static int moveForwardArm = 1;
		
		if(leftUpperArmRotation == 65){
			moveForwardArm = 1;
		}
		if(leftUpperArmRotation == 125){
			moveForwardArm = 0;
		}
		
		if(moveForwardArm){
			leftUpperArmRotation += 5;
		}else{
			leftUpperArmRotation -= 5;
		}
		
		leftUpperArmEndPoint = lengthEndPoint(coord(center.x, centerPositionY), leftUpperArmRotation, leftUpperArmLength);
		plotLine(frame, center.x, centerPositionY, leftUpperArmEndPoint.x, leftUpperArmEndPoint.y, color);
	}
	
	// left lower arm
	Coord leftLowerArmEndPoint = lengthEndPoint(coord(leftUpperArmEndPoint.x, leftUpperArmEndPoint.y), leftUpperArmRotation + 30, leftLowerArmLength);
	plotLine(frame, leftUpperArmEndPoint.x, leftUpperArmEndPoint.y, leftLowerArmEndPoint.x, leftLowerArmEndPoint.y, color);
	
	// right upper leg
	Coord rightUpperLegEndPoint;
	static int rightUpperLegRotation = 125;
	{
		static int moveBackwardLeg = 1;
		
		if(rightUpperLegRotation == 125){
			moveBackwardLeg = 1;
		}
		if(rightUpperLegRotation == 65){
			moveBackwardLeg = 0;
		}
		
		if(moveBackwardLeg){
			rightUpperLegRotation -= 5;
		}else{
			rightUpperLegRotation += 5;
		}
		
		rightUpperLegEndPoint = lengthEndPoint(coord(bodyEndPoint.x, bodyEndPoint.y), rightUpperLegRotation, rightUpperLegLength);
		plotLine(frame, bodyEndPoint.x, bodyEndPoint.y, rightUpperLegEndPoint.x, rightUpperLegEndPoint.y, color);
	}
	
	// right lower leg
	{
		static int rightLowerLegRotation = 95;
		static int moveBackwardLeg = 1;
		
		if(rightUpperLegRotation == 125){
			moveBackwardLeg = 1;
			rightLowerLegRotation = 95;
		}
		if(rightUpperLegRotation == 65){
			moveBackwardLeg = 0;
			rightLowerLegRotation = 70;
		}
		
		if(rightUpperLegRotation <= 90 ){
			if(moveBackwardLeg){
				rightLowerLegRotation = rightUpperLegRotation;
			}else{
				rightLowerLegRotation -= 5;
			}
		}else{
			if(!moveBackwardLeg){
				rightLowerLegRotation += 10;
				if(centerPositionY <= center.y + 1){
					centerPositionY++;
				}
			}else{
				if(centerPositionY > center.y){
					centerPositionY--;
				}
			}
		}

		Coord rightLowerLegEndPoint = lengthEndPoint(coord(rightUpperLegEndPoint.x, rightUpperLegEndPoint.y), rightLowerLegRotation, rightLowerLegLength);
		plotLine(frame, rightUpperLegEndPoint.x, rightUpperLegEndPoint.y, rightLowerLegEndPoint.x, rightLowerLegEndPoint.y, color);
	}
	
	// left upper leg
	Coord leftUpperLegEndPoint;
	static int leftUpperLegRotation = 65;
	{
		static int moveForwardLeg = 1;
		
		if(leftUpperLegRotation == 125){
			moveForwardLeg = 0;
		}
		if(leftUpperLegRotation == 65){
			moveForwardLeg = 1;
		}
		
		if(moveForwardLeg){
			leftUpperLegRotation += 5;
		}else{
			leftUpperLegRotation -= 5;
		}
		
		leftUpperLegEndPoint = lengthEndPoint(coord(bodyEndPoint.x, bodyEndPoint.y), leftUpperLegRotation, leftUpperLegLength);
		plotLine(frame, bodyEndPoint.x, bodyEndPoint.y, leftUpperLegEndPoint.x, leftUpperLegEndPoint.y, color);
	}
	
	// left lower leg
	{
		static int leftLowerLegRotation = 70;
		static int moveForwardLeg = 1;
		
		if(leftUpperLegRotation == 125){
			moveForwardLeg = 0;
			leftLowerLegRotation = 95;
		}
		if(leftUpperLegRotation == 65){
			moveForwardLeg = 1;
			leftLowerLegRotation = 70;
		}
		
		if(leftUpperLegRotation <= 90 ){
			if(moveForwardLeg){
				leftLowerLegRotation -= 5;
			}else{
				leftLowerLegRotation = leftUpperLegRotation;
			}
		}else{
			if(moveForwardLeg){
				leftLowerLegRotation += 10;
				if(centerPositionY <= center.y + 1){
					centerPositionY++;
				}
			}else{
				if(centerPositionY > center.y){
					centerPositionY--;
				}
			}
		}
		
		Coord leftLowerLegEndPoint = lengthEndPoint(coord(leftUpperLegEndPoint.x, leftUpperLegEndPoint.y), leftLowerLegRotation, leftLowerLegLength);
		plotLine(frame, leftUpperLegEndPoint.x, leftUpperLegEndPoint.y, leftLowerLegEndPoint.x, leftLowerLegEndPoint.y, color);
	}
}

/* MAIN FUNCTION ------------------------------------------------------- */
int main() {	
	/* Preparations ---------------------------------------------------- */
	
	// get fb and screenInfos
	struct fb_var_screeninfo vInfo; // variable screen info
	struct fb_fix_screeninfo sInfo; // static screen info
	int fbFile;	 // frame buffer file descriptor
	fbFile = open("/dev/fb0",O_RDWR);
	if (!fbFile) {
		printf("Error: cannot open framebuffer device.\n");
		exit(1);
	}
	if (ioctl (fbFile, FBIOGET_FSCREENINFO, &sInfo)) {
		printf("Error reading fixed information.\n");
		exit(2);
	}
	if (ioctl (fbFile, FBIOGET_VSCREENINFO, &vInfo)) {
		printf("Error reading variable information.\n");
		exit(3);
	}
	
	// create the FrameBuffer struct with its important infos.
	FrameBuffer fb;
	fb.smemLen = sInfo.smem_len;
	fb.lineLen = sInfo.line_length;
	fb.bpp = vInfo.bits_per_pixel;
	
	// and map the framebuffer to the FB struct.
	fb.ptr = (char*)mmap(0, sInfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbFile, 0);
	if ((long int)fb.ptr == -1) {
		printf ("Error: failed to map framebuffer device to memory.\n");
		exit(4);
	}
	
	// prepare mouse controller
	FILE *fmouse;
	char mouseRaw[3];
	fmouse = fopen("/dev/input/mice","r");
	Coord mouse; // mouse internal counter
	mouse.x = 0;
	mouse.y = 0;
		
	// prepare environment controller
	unsigned char loop = 1; // frame loop controller
	Frame cFrame; // composition frame (Video RAM)
	
	// prepare canvas
	Frame canvas;
	flushFrame(&canvas, rgb(0,0,0));
	int canvasWidth = 1100;
	int canvasHeight = 600;
	Coord canvasPosition = coord(screenX/2,screenY/2);
		
	// prepare plane
	int planeVelocity = 10;

	int planeXPosition = canvasWidth;
	int planeYPosition = 50;
	int explosionMul = 0;
	
	// prepare ammunition
	/*Coord firstAmmunitionCoordinate;
	int isFirstAmmunitionReleased = 1;
	Coord secondAmmunitionCoordinate;
	int isSecondAmmunitionReleased = 0;
	int ammunitionVelocity = 5;
	int ammunitionLength = 20;
	
	firstAmmunitionCoordinate.x = shipXPosition;
	firstAmmunitionCoordinate.y = shipYPosition - 120;
	secondAmmunitionCoordinate.y = shipYPosition - 120;
	
	
	//prepare Bomb
	Coord firstBombCoordinate;
	int isFirstBombReleased = 1;
	Coord secondBombCoordinate;
	int isSecondBombReleased = 0;
	int bombVelocity = 10;
	int bombLength = 20;
	
	firstBombCoordinate.x = planeXPosition;
	firstBombCoordinate.y = planeYPosition + 120;
	secondBombCoordinate.y = planeYPosition - 120;*/
	
	
	int i; //for drawing.
	int MoveLeft = 1;
	
	int isXploded = 0;
	Coord coordXplosion;
	
	int size = 25;
	int chuteX = 400;
	int chuteY = 50;
	int stickmanX = 1150;
	
	/* Main Loop ------------------------------------------------------- */
	
	while (loop) {
		
		// clean composition frame
		flushFrame(&cFrame, rgb(33,33,33));
				
		showCanvas(&cFrame, &canvas, canvasWidth, canvasHeight, canvasPosition, rgb(99,99,99), 1);
		
		// clean canvas
		flushFrame(&canvas, rgb(0,0,0));
		
		// draw plane
		drawPlane(&canvas, coord(planeXPosition -= planeVelocity, planeYPosition), rgb(99, 99, 99));
		
		drawParachute(&canvas, coord(chuteX+=4, chuteY+=1), rgb(99, 99, 99), 300);
		
		drawWalkingStickman(&canvas, coord(stickmanX-=4, 503), rgb(99, 99, 99));
		
		// stickman ammunition
		/*if(isFirstAmmunitionReleased){
			firstAmmunitionCoordinate.y-=ammunitionVelocity;
			
			if(firstAmmunitionCoordinate.y <= canvasHeight/3 && !isSecondAmmunitionReleased){
				isSecondAmmunitionReleased = 1;
				secondAmmunitionCoordinate.x = shipXPosition;
				secondAmmunitionCoordinate.y = shipYPosition - 120;
			}
			
			if(firstAmmunitionCoordinate.y <= -ammunitionLength){
				isFirstAmmunitionReleased = 0;
			}
			
			drawPeluru(&canvas, firstAmmunitionCoordinate, rgb(99, 99, 99));
			drawAmmunition(&canvas, firstAmmunitionCoordinate, 3, ammunitionLength, rgb(99, 99, 99));
		}
		
		if(isSecondAmmunitionReleased){
			secondAmmunitionCoordinate.y-=ammunitionVelocity;
			
			if(secondAmmunitionCoordinate.y <= canvasHeight/3 && !isFirstAmmunitionReleased){
				isFirstAmmunitionReleased = 1;
				firstAmmunitionCoordinate.x = shipXPosition;
				firstAmmunitionCoordinate.y = shipYPosition - 120;
			}
			
			if(secondAmmunitionCoordinate.y <= 0){
				isSecondAmmunitionReleased = 0;
			}
			
			drawPeluru(&canvas, secondAmmunitionCoordinate, rgb(99, 99, 99));
			drawAmmunition(&canvas, secondAmmunitionCoordinate, 3, ammunitionLength, rgb(99, 99, 99));
		}*/
			
		//explosion
		/*if (isInBound(coord(firstAmmunitionCoordinate.x, firstAmmunitionCoordinate.y), coord(planeXPosition-5, planeYPosition-15), coord(planeXPosition+170, planeYPosition+15))) {
			coordXplosion = firstAmmunitionCoordinate;
			isXploded = 1;
			//printf("boom");
		} else if (isInBound(coord(secondAmmunitionCoordinate.x, secondAmmunitionCoordinate.y), coord(planeXPosition-5, planeYPosition-15), coord(planeXPosition+170, planeYPosition+15))) {
			coordXplosion = secondAmmunitionCoordinate;
			isXploded = 1;
			//printf("boom");
		}
		else if (isInBound(coord(firstBombCoordinate.x, firstBombCoordinate.y), coord(shipXPosition-50, shipYPosition-100), coord(shipXPosition+50, shipYPosition+30))) {
			coordXplosion = firstBombCoordinate;
			isXploded = 1;
			//printf("boom");
		} else if (isInBound(coord(secondBombCoordinate.x, secondBombCoordinate.y), coord(shipXPosition-50, shipYPosition-100), coord(shipXPosition+50, shipYPosition+30))) {
			coordXplosion = secondBombCoordinate;
			isXploded = 1;
			//printf("boom");
		}
		if (isXploded == 1) {
			animateExplosion(&canvas, explosionMul, coordXplosion);
			explosionMul++;
			if(explosionMul >= 20){
				explosionMul = 0;
				isXploded = 0;
			}
		}*/
		
		if(planeXPosition <= -170){
			planeXPosition = canvasWidth;
		}
		
		if(planeXPosition == screenX/2 - canvasWidth/2 - 165){
			planeXPosition = screenX/2 + canvasWidth/2;
		}
		
		//show frame
		showFrame(&cFrame,&fb);
		
	}

	/* Cleanup --------------------------------------------------------- */
	munmap(fb.ptr, sInfo.smem_len);
	close(fbFile);
	fclose(fmouse);
	return 0;
}
