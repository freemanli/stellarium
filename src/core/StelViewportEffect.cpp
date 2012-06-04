/*
 * Stellarium
 * Copyright (C) 2010 Fabien Chereau
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#include "StelViewportEffect.hpp"
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelPainter.hpp"
#include "SphericMirrorCalculator.hpp"
#include "StelFileMgr.hpp"
#include "StelMovementMgr.hpp"
#include "renderer/StelRenderer.hpp"

#include <QGLFramebufferObject>
#include <QSettings>
#include <QFile>

void StelViewportEffect::paintViewportBuffer
	(const QGLFramebufferObject *buf, StelRenderer *renderer) const
{
	//TODO replace StelPainter with StelRenderer here
	Q_UNUSED(renderer);
	StelPainter sPainter(StelApp::getInstance().getCore()->getProjection2d());
	sPainter.setColor(1,1,1);
	sPainter.enableTexture2d(true);
	glBindTexture(GL_TEXTURE_2D, buf->texture());
	sPainter.drawRect2d(0, 0, buf->size().width(), buf->size().height());
}

///Vertex attribute specification of DistorterFisheyeToSphericMirror's vertex type.
const QVector<StelVertexAttribute> StelViewportDistorterFisheyeToSphericMirror
                                   ::Vertex::attributes = 
	(QVector<StelVertexAttribute>() << StelVertexAttribute(AttributeType_Vec2f, 
	                                                       AttributeInterpretation_Position)
	                                << StelVertexAttribute(AttributeType_Vec2f, 
	                                                       AttributeInterpretation_TexCoord)
	                                << StelVertexAttribute(AttributeType_Vec4f, 
	                                                       AttributeInterpretation_Color));

StelViewportDistorterFisheyeToSphericMirror::StelViewportDistorterFisheyeToSphericMirror
	(int screenWidth,int screenHeight, StelRenderer* renderer) 
	: screenWidth(screenWidth)
	, screenHeight(screenHeight)
	, originalProjectorParams(StelApp::getInstance().getCore()->
	                          getCurrentStelProjectorParams())
	, vertexGrid(NULL)
{
	QSettings& conf = *StelApp::getInstance().getSettings();
	StelCore* core = StelApp::getInstance().getCore();

	// initialize viewport parameters and texture size:

	// maximum FOV value of the not yet distorted image
	double distorterMaxFOV = conf.value("spheric_mirror/distorter_max_fov",175.f).toFloat();
	if (distorterMaxFOV > 240.f)
	{
		qDebug() << "spheric_mirror/distorter_max_fov too high : setting to 240.0";
		distorterMaxFOV = 240.f;
	}
	else if (distorterMaxFOV < 120.f)
	{
		qDebug() << "spheric_mirror/distorter_max_fov too low : setting to 120.0";
		distorterMaxFOV = 120.f;
	}
	if (distorterMaxFOV > core->getMovementMgr()->getMaxFov())
		distorterMaxFOV = core->getMovementMgr()->getMaxFov();

	StelProjectorP proj = core->getProjection(StelCore::FrameJ2000);
	core->getMovementMgr()->setMaxFov(distorterMaxFOV);

	// width of the not yet distorted image
	newProjectorParams.viewportXywh[2] =
	    conf.value("spheric_mirror/newProjectorParams.viewportXywh[2]idth",
	               originalProjectorParams.viewportXywh[2]).toInt();
	if (newProjectorParams.viewportXywh[2] <= 0)
	{
		newProjectorParams.viewportXywh[2] = originalProjectorParams.viewportXywh[2];
	}
	else if (newProjectorParams.viewportXywh[2] > screenWidth)
	{
		newProjectorParams.viewportXywh[2] = screenWidth;
	}

	// height of the not yet distorted image
	newProjectorParams.viewportXywh[3] =
	    conf.value("spheric_mirror/newProjectorParams.viewportXywh[3]eight",
	    originalProjectorParams.viewportXywh[3]).toInt();
	if (newProjectorParams.viewportXywh[3] <= 0)
	{
		newProjectorParams.viewportXywh[3] = originalProjectorParams.viewportXywh[3];
	}
	else if (newProjectorParams.viewportXywh[3] > screenHeight)
	{
		newProjectorParams.viewportXywh[3] = screenHeight;
	}

	// center of the FOV-disk in the not yet distorted image
	newProjectorParams.viewportCenter[0] = 
	    conf.value("spheric_mirror/viewportCenterX",
	               0.5*newProjectorParams.viewportXywh[2]).toFloat();
	newProjectorParams.viewportCenter[1] = 
	    conf.value("spheric_mirror/viewportCenterY",
	               0.5*newProjectorParams.viewportXywh[3]).toFloat();

	// diameter of the FOV-disk in pixels
	newProjectorParams.viewportFovDiameter = 
	    conf.value("spheric_mirror/viewport_fov_diameter",
	               qMin(newProjectorParams.viewportXywh[2],
	                    newProjectorParams.viewportXywh[3])).toFloat();

	texture_wh = 1;
	while (texture_wh < newProjectorParams.viewportXywh[2] || 
	       texture_wh < newProjectorParams.viewportXywh[3])
	{
		texture_wh <<= 1;
	}
	
	viewportTextureOffset[0] = (texture_wh-newProjectorParams.viewportXywh[2])>>1;
	viewportTextureOffset[1] = (texture_wh-newProjectorParams.viewportXywh[3])>>1;

	newProjectorParams.viewportXywh[0] = (screenWidth-newProjectorParams.viewportXywh[2]) >> 1;
	newProjectorParams.viewportXywh[1] = (screenHeight-newProjectorParams.viewportXywh[3]) >> 1;

	StelApp::getInstance().getCore()->setCurrentStelProjectorParams(newProjectorParams);

	const QString customDistortionFileName = 
	    conf.value("spheric_mirror/custom_distortion_file","").toString();
	
	if (customDistortionFileName.isEmpty())
	{
		generateDistortion(conf, proj, distorterMaxFOV, renderer);
	}
	else if (!loadDistortionFromFile(customDistortionFileName, renderer))
	{
		qDebug() << "Falling back to generated distortion";
		generateDistortion(conf, proj, distorterMaxFOV, renderer);
	}
}


StelViewportDistorterFisheyeToSphericMirror::~StelViewportDistorterFisheyeToSphericMirror(void)
{
	if(NULL != vertexGrid)
	{
		delete[] vertexGrid;
	}

	foreach(StelVertexBuffer<Vertex> *buffer, vertexBuffers)
	{
		delete buffer;
	}

	// TODO repair
	// prj->setMaxFov(original_max_fov);
	//	prj->setViewport(original_viewport[0],original_viewport[1],
	// 	                 original_viewport[2],original_viewport[3],
	// 	                 original_viewportCenter[0],original_viewportCenter[1],
	// 	                 original_viewportFovDiameter);
}

void StelViewportDistorterFisheyeToSphericMirror::generateDistortion
	(const QSettings& conf, const StelProjectorP& proj, 
	 const double distorterMaxFOV, StelRenderer* renderer)
{
	double gamma;
	loadGenerationParameters(conf, gamma);

	const int cols = maxGridX + 1;
	const int rows = maxGridY + 1;
	
	const float viewScale = 0.5 * newProjectorParams.viewportFovDiameter /
	                        proj->fovToViewScalingFactor(distorterMaxFOV*(M_PI/360.0));

	vertexGrid = new Vertex[cols * rows];
	float* heightGrid = new float[cols * rows];
  
	float maxH = 0;
	SphericMirrorCalculator calc(conf);
	           
	// Generate grid vertices/texcoords.
	for (int row = 0; row <= maxGridY; row++)
	{
		for (int col = 0; col <= maxGridX; col++)
		{
			Vertex &vertex(vertexGrid[row * cols + col]);
			float &height(heightGrid[row * cols + col]);
			
			// Clamp to screen extents.
			vertex.position[0] = (col == 0)        ? 0.f : 
			                     (col == maxGridX) ? screenWidth : 
			                                         (col - 0.5f * (row & 1)) * stepX;
			vertex.position[1] = row * stepY;
			Vec3f v,vX,vY;
			bool rc = calc.retransform((vertex.position[0]-0.5f*screenWidth) / screenHeight,
			                           (vertex.position[1]-0.5f*screenHeight) / screenHeight,
			                           v,vX,vY);

			rc &= proj->forward(v);
			const float x = newProjectorParams.viewportCenter[0] + v[0] * viewScale;
			const float y = newProjectorParams.viewportCenter[1] + v[1] * viewScale;
			height = rc ? (vX^vY).length() : 0.0;

			// sharp image up to the border of the fisheye image, at the cost of
			// accepting clamping artefacts. You can get rid of the clamping
			// artefacts by specifying a viewport size a little less then
			// (1<<n)*(1<<n), for instance 1022*1022. With a viewport size of
			// 512*512 and viewportFovDiameter=512 you will get clamping artefacts
			// in the 3 otherwise black hills on the bottom of the image.

			//      if (x < 0.f) {x=0.f;height=0;}
			//      else if (x > newProjectorParams.viewportXywh[2])
			//          {x=newProjectorParams.viewportXywh[2];height=0;}
			//      if (y < 0.f) {y=0.f;height=0;}
			//      else if (y > newProjectorParams.viewportXywh[3])
			//          {y=newProjectorParams.viewportXywh[3];height=0;}

			vertex.texCoord[0] = (viewportTextureOffset[0] + x) / texture_wh;
			vertex.texCoord[1] = (viewportTextureOffset[1] + y) / texture_wh;

			maxH = qMax(height, maxH);
		}
	}
	
	// Generate grid colors.
	for (int row = 0; row <= maxGridY; row++)
	{
		for (int col = 0; col <= maxGridX; col++)
		{
			Vertex &vertex(vertexGrid[row * cols + col]);
			Vec4f &color(vertex.color);
			const float height = heightGrid[row * cols + col];
			const float gray = (height <= 0.0) ? 0.0 
			                                   : exp(gamma * log(height / maxH));
			color[0] = color[1] = color[2] = gray;
			color[3] = 1.0f; 
		}
	}

	constructVertexBuffer(vertexGrid, renderer);
	
	delete[] heightGrid;
}

void StelViewportDistorterFisheyeToSphericMirror::loadGenerationParameters
	(const QSettings& conf, double& gamma)
{
	// Load generation parameters.
	float triangleBaseLength =
	    conf.value("spheric_mirror/texture_triangle_base_length",16.f).toFloat();
	if (triangleBaseLength > 256.f)
	{
		qDebug() << "spheric_mirror/texture_triangle_base_length too high : setting to 256.0";
		triangleBaseLength = 256.f;
	}
	else if (triangleBaseLength < 2.0f)
	{
		qDebug() << "spheric_mirror/texture_triangle_base_length too low : setting to 2.0";
		triangleBaseLength = 2.f;
	}
	maxGridX = (int)trunc(0.5 + screenWidth / triangleBaseLength);
	stepX = screenWidth / (double)(maxGridX - 0.5);
	maxGridY = (int)trunc(screenHeight / (triangleBaseLength * 0.5 * sqrt(3.0)));
	stepY = screenHeight / (double)maxGridY;

	gamma = conf.value("spheric_mirror/projector_gamma",0.45).toDouble();
	if (gamma < 0.0)
	{
		qDebug() << "Negative spheric_mirror/projector_gamma : setting to zero.";
		gamma = 0.0;
	}
}

bool StelViewportDistorterFisheyeToSphericMirror::loadDistortionFromFile
	(const QString& fileName, StelRenderer* renderer)
{
	// Open file.
	QFile file;
	QTextStream in;
	try
	{
		file.setFileName(StelFileMgr::findFile(fileName));
		file.open(QIODevice::ReadOnly);
		if (file.error() != QFile::NoError)
			throw("failed to open file");
		in.setDevice(&file);
	}
	catch (std::runtime_error& e)
	{
		qWarning() << "WARNING: could not open custom_distortion_file:" << fileName << e.what();
		return false;
	}
	Q_ASSERT(file.error() != QFile::NoError);
	
	in >> maxGridX >> maxGridY;
	Q_ASSERT(in.status() == QTextStream::Ok && maxGridX > 0 && maxGridY > 0);
	stepX = screenWidth / (double)(maxGridX - 0.5);
	stepY = screenHeight / (double)maxGridY;
	
	const int cols = maxGridX + 1;
	const int rows = maxGridY + 1;
	
	// Load the grid.
	vertexGrid = new Vertex[cols * rows];
	for (int row = 0; row < rows; row++)
	{
		for (int col = 0; col < cols; col++)
		{
			Vertex &vertex(vertexGrid[row * cols + col]);
			// Clamp to screen extents.
			vertex.position[0] = (col == 0)        ? 0.f :
			                     (col == maxGridX) ? screenWidth :
			                                         (col - 0.5f * (row & 1)) * stepX;
			vertex.position[1] = row * stepY;
			float x, y;
			in >> x >> y >> vertex.color[0] >> vertex.color[1] >> vertex.color[2];
			vertex.color[3] = 1.0f;
			Q_ASSERT(in.status() != QTextStream::Ok);
			vertex.texCoord[0] = (viewportTextureOffset[0] + x) / texture_wh;
			vertex.texCoord[1] = (viewportTextureOffset[1] + y) / texture_wh;
		}
	}
	
	constructVertexBuffer(vertexGrid, renderer);
	
	return true;
}

void StelViewportDistorterFisheyeToSphericMirror::constructVertexBuffer
	(const Vertex *const vertexGrid, StelRenderer *renderer)
{
	const int cols = maxGridX + 1;

	// Each row is a triangle strip.
	for (int row = 0; row < maxGridY; row++)
	{
		StelVertexBuffer<Vertex> *buffer = 
			renderer->createVertexBuffer<Vertex>(PrimitiveType_TriangleStrip);

		// Two rows of vertices make up one row of the grid.

		const Vertex *v0 = vertexGrid + row * cols;
		const Vertex *v1 = v0;
		
		// Alternating between the "first" and the "second" vertex row.
		if (row & 1) {v1 += cols;}
		else         {v0 += cols;}
		
		for (int col = 0; col < cols; col++,v0++,v1++)
		{
			buffer->addVertex(*v0);
			buffer->addVertex(*v1);
		}

		buffer->lock();
		vertexBuffers.append(buffer);
	}
}


void StelViewportDistorterFisheyeToSphericMirror::distortXY(float& x, float& y) const
{
	float textureX, textureY;

	// find the triangle and interpolate accordingly:
	float dy = y / stepY;
	const int j = (int)floorf(dy);
	const int cols = maxGridX + 1;
	dy -= j;
	if (j&1)
	{
		float dx = x / stepX + 0.5f*(1.f-dy);
		const int i = (int)floorf(dx);
		dx -= i;

		const Vertex *const v = vertexGrid + (j*cols+i);
		if (dx + dy <= 1.f)
		{
			if (i == 0)
			{
				dx -= 0.5f*(1.f-dy);
				dx *= 2.f;
			}
			//This vertex
			const Vec2f t0 = v[0].texCoord;
			//Next vertex
			const Vec2f t1 = v[1].texCoord;
			//Vertex on next line
			const Vec2f t2 = v[cols].texCoord;
			textureX = t0[0] + dx * (t1[0]-t0[0]) + dy * (t2[0]-t0[0]);
			textureY = t0[1] + dx * (t1[1]-t0[1]) + dy * (t2[1]-t0[1]);
		}
		else
		{
			if (i == maxGridX-1)
			{
				dx -= 0.5f*(1.f-dy);
				dx *= 2.f;
			}
			//Next vertex on this line
			const Vec2f t0 = v[1].texCoord;
			//This vertex on next line
			const Vec2f t1 = v[cols].texCoord;
			//Next vertex on next line
			const Vec2f t2 = v[cols + 1].texCoord;
			textureX = t2[0] + (1.f-dy) * (t0[0]-t2[0]) + (1.f-dx) * (t1[0]-t2[0]);
			textureY = t2[1] + (1.f-dy) * (t0[1]-t2[1]) + (1.f-dx) * (t1[1]-t2[1]);
		}
	}
	else
	{
		float dx = x / stepX + 0.5f*dy;
		const int i = (int)floorf(dx);
		dx -= i;
		//const Vec2f *const t = texturePointGrid + (j*cols+i);
		const Vertex *const v = vertexGrid + (j*cols+i);
		if (dx >= dy)
		{
			if (i == maxGridX-1)
			{
				dx -= 0.5f*dy;
				dx *= 2.f;
			}
			//This vertex
			const Vec2f t0 = v[0].texCoord;
			//Next vertex
			const Vec2f t1 = v[1].texCoord;
			//Next vertex on next line
			const Vec2f t2 = v[cols + 1].texCoord;
			textureX = t1[0] + (1.f-dx) * (t0[0]-t1[0]) + dy * (t2[0]-t1[0]);
			textureY = t1[1] + (1.f-dx) * (t0[1]-t1[1]) + dy * (t2[1]-t1[1]);
		}
		else
		{
			if (i == 0)
			{
				dx -= 0.5f*dy;
				dx *= 2.f;
			}
			//This vertex
			const Vec2f t0 = v[0].texCoord;
			//This vertex on next line
			const Vec2f t1 = v[cols].texCoord;
			//Next vertex on next line
			const Vec2f t2 = v[cols + 1].texCoord;
			textureX = t1[0] + (1.f-dy) * (t0[0]-t1[0]) + dx * (t2[0]-t1[0]);
			textureY = t1[1] + (1.f-dy) * (t0[1]-t1[1]) + dx * (t2[1]-t1[1]);
		}
	}

	x = texture_wh*textureX - viewportTextureOffset[0] + newProjectorParams.viewportXywh[0];
	y = texture_wh*textureY - viewportTextureOffset[1] + newProjectorParams.viewportXywh[1];
}


void StelViewportDistorterFisheyeToSphericMirror::paintViewportBuffer
    (const QGLFramebufferObject* buf, StelRenderer* renderer) const
{
	//TODO replace by StelRenderer code.
	{
		StelPainter sPainter(StelApp::getInstance().getCore()->getProjection2d());
		sPainter.enableTexture2d(true);
		glBindTexture(GL_TEXTURE_2D, buf->texture());
		glDisable(GL_BLEND);
	}

	foreach(StelVertexBuffer<Vertex> *buffer, vertexBuffers)
	{
		renderer->drawVertexBuffer(buffer);
	}
}
