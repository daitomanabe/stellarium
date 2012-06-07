#ifndef _STELQGLVIEWPORT_HPP_
#define _STELQGLVIEWPORT_HPP_

#include <QGLFramebufferObject>
#include <QGLWidget>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>

#include "StelUtils.hpp"

//! GLWidget specialized for Stellarium, mostly to provide better debugging information.
class StelQGLWidget : public QGLWidget
{
public:
	StelQGLWidget(QGLContext* ctx, QWidget* parent) : QGLWidget(ctx, parent)
	{
		setAttribute(Qt::WA_PaintOnScreen);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);
		//setAutoFillBackground(false);
		setBackgroundRole(QPalette::Window);
	}

protected:
	virtual void initializeGL()
	{
		qDebug() << "OpenGL supported version: " << QString((char*)glGetString(GL_VERSION));

		QGLWidget::initializeGL();

		if (!format().stencil())
			qWarning("Could not get stencil buffer; results will be suboptimal");
		if (!format().depth())
			qWarning("Could not get depth buffer; results will be suboptimal");
		if (!format().doubleBuffer())
			qWarning("Could not get double buffer; results will be suboptimal");

		QString paintEngineStr;
		switch (paintEngine()->type())
		{
			case QPaintEngine::OpenGL:  paintEngineStr = "OpenGL"; break;
			case QPaintEngine::OpenGL2: paintEngineStr = "OpenGL2"; break;
			default:                    paintEngineStr = "Other";
		}
		qDebug() << "Qt GL paint engine is: " << paintEngineStr;
	}
};

//! Manages OpenGL viewport.
//!
//! This class handles things like framebuffers and Qt-style painting to the viewport.
class StelQGLViewport
{
public:
	//! Construct a StelQGLViewport using specified widget.
	//!
	//! @param glWidget GL widget that contains the viewport.
	//! @param parent Parent widget of glWidget.
	StelQGLViewport(StelQGLWidget* const glWidget, QGraphicsView* const parent)
		: viewportSize(QSize())
		, glWidget(glWidget)
		, painter(NULL)
		, defaultPainter(NULL)
		, backBufferPainter(NULL)
		, frontBuffer(NULL)
		, backBuffer(NULL)
		, drawing(false)
		, usingGLWidgetPainter(false)
		, fboSupported(false)
		, fboDisabled(false)
		, nonPowerOfTwoTexturesSupported(false)
	{
		// Forces glWidget to initialize GL.
		glWidget->updateGL();
		parent->setViewport(glWidget);
		invariant();
	}

	//! Destroy the StelQGLViewport.
	~StelQGLViewport()
	{
		invariant();
		Q_ASSERT_X(NULL == this->painter, Q_FUNC_INFO, 
		           "Painting is not disabled at destruction");

		destroyFBOs();
		// No need to delete the GL widget, its parent widget will do that.
		glWidget = NULL;
	}

	//! Initialize the viewport.
	void init(const bool npot)
	{
		invariant();
		this->nonPowerOfTwoTexturesSupported = npot;
		// Prevent flickering on mac Leopard/Snow Leopard
		glWidget->setAutoFillBackground(false);
		fboSupported = QGLFramebufferObject::hasOpenGLFramebufferObjects();
		invariant();
	}

	//! Called when viewport size changes so we can replace the FBOs.
	void viewportHasBeenResized(const QSize newSize)
	{
		invariant();
		//Can't check this in invariant because Renderer is initialized before 
		//AppGraphicsWidget sets its viewport size
		Q_ASSERT_X(newSize.isValid(), Q_FUNC_INFO, "Invalid scene size");
		viewportSize = newSize;
		//We'll need FBOs of different size so get rid of the current FBOs.
		destroyFBOs();
		invariant();
	}
	
	//! Set the default painter to use when not drawing to FBO.
	void setDefaultPainter(QPainter* const painter)
	{
		invariant();
		defaultPainter = painter;
		invariant();
	}

	//! Grab a screenshot.
	QImage screenshot() const 
	{
		invariant();
		return glWidget->grabFrameBuffer();
	}

	//! Return current viewport size in pixels.
	QSize getViewportSize() const 
	{
		invariant();
		return viewportSize;
	}

	//! Get a texture of the viewport.
	StelTextureBackend* getViewportTextureBackend(class StelQGLRenderer* renderer);

	//! Are we using framebuffer objects?
	bool useFBO() const
	{
		// Can't call invariant here as invariant calls useFBO
		return fboSupported && !fboDisabled;
	}

	//! Start using drawing calls.
	void startFrame()
	{
		invariant();
		if (useFBO())
		{
			//Draw to backBuffer.
			initFBOs();
			backBuffer->bind();
			backBufferPainter = new QPainter(backBuffer);
			enablePainting(backBufferPainter);
		}
		else
		{
			enablePainting();
		}
		drawing = true;
		invariant();
	}

	// Separate from finishFrame only for readability
	//! Suspend drawing, not showing the result on the screen.
	//!
	//! Finishes using draw calls for this frame. 
	//! Drawing can continue later. Only usable with FBOs.
	void suspendFrame() {finishFrame(false);}
	
	//! Finish using draw calls.
	void finishFrame(const bool swapBuffers = true)
	{
		invariant();
		drawing = false;

		disablePainting();
		
		if (useFBO())
		{
			//Release the backbuffer.
			delete backBufferPainter;
			backBufferPainter = NULL;

			backBuffer->release();
			//Swap buffers if finishing, don't swap yet if suspending.
			if(swapBuffers){swapFBOs();}
		}
		invariant();
	}

	//! Code that must run before drawing the final result of the rendering to the viewport.
	void prepareToDrawViewport()
	{
		invariant();
		//Put the result of drawing to the FBO on the screen, applying an effect.
		if (useFBO())
		{
			Q_ASSERT_X(!backBuffer->isBound() && !frontBuffer->isBound(), Q_FUNC_INFO, 
			           "Framebuffer objects weren't released before drawing the result");
		}
	}

	//! Disable Qt-style painting.
	void disablePainting()
	{
		invariant();
		Q_ASSERT_X(NULL != painter, Q_FUNC_INFO, "Painting is already disabled");
		
		StelPainter::setQPainter(NULL);
		if(usingGLWidgetPainter)
		{
			delete painter;
			usingGLWidgetPainter = false;
		}
		painter = NULL;
		invariant();
	}

	//! Enable Qt-style painting (with the current default painter, or constructing a fallback if no default).
	void enablePainting()
	{
		invariant();
		enablePainting(defaultPainter);
		invariant();
	}

private:
	//! Size of the viewport (i.e. graphics resolution).
	QSize viewportSize;
	//! Widget we're drawing to with OpenGL.
	StelQGLWidget* glWidget;

	//! Painter we're currently using to paint. NULL if painting is disabled.
	QPainter* painter;
	//! Painter we're using if not using FBOs or when not drawing to an FBO.
	QPainter* defaultPainter;
	//! Painter to the FBO we're drawing to, when using FBOs.
	QPainter* backBufferPainter;

	//! Frontbuffer (i.e. displayed at the moment) frame buffer object, when using FBOs.
	QGLFramebufferObject* frontBuffer;
	//! Backbuffer (i.e. drawn to at the moment) frame buffer object, when using FBOs.
	QGLFramebufferObject* backBuffer;

	//! Are we in the middle of drawing a frame?
	bool drawing;

	//! Are we using the fallback painter (directly to glWidget) ?
	bool usingGLWidgetPainter;

	//! Are frame buffer objects supported on this system?
	bool fboSupported;
	//! Disable frame buffer objects even if supported?
	//!
	//! Currently, this is only used for debugging. 
	//! It might be loaded from a config file later.
	bool fboDisabled;

	//! Are non power of two textures supported?
	bool nonPowerOfTwoTexturesSupported;

	//! Asserts that we're in a valid state.
	void invariant() const
	{
		Q_ASSERT_X(NULL != glWidget, Q_FUNC_INFO, "Destroyed StelQGLViewport");
		Q_ASSERT_X(glWidget->isValid(), Q_FUNC_INFO, 
		           "Invalid glWidget (maybe there is no OpenGL support?)");

		const bool fbo = useFBO();
		Q_ASSERT_X(NULL == backBufferPainter || fbo, Q_FUNC_INFO,
		           "We have a backbuffer painter even though we're not using FBO");
		Q_ASSERT_X(drawing && fbo ? backBufferPainter != NULL : true, Q_FUNC_INFO,
		           "We're drawing and using FBOs, but the backBufferPainter is NULL");
		Q_ASSERT_X(NULL == backBuffer || fbo, Q_FUNC_INFO,
		           "We have a backbuffer even though we're not using FBO");
		Q_ASSERT_X(NULL == frontBuffer || fbo, Q_FUNC_INFO,
		           "We have a frontbuffer even though we're not using FBO");
		Q_ASSERT_X(drawing && fbo ? backBuffer != NULL : true, Q_FUNC_INFO,
		           "We're drawing and using FBOs, but the backBuffer is NULL");
		Q_ASSERT_X(drawing && fbo ? frontBuffer != NULL : true, Q_FUNC_INFO,
		           "We're drawing and using FBOs, but the frontBuffer is NULL");
	}

	//! Enable Qt-style painting with specified painter (or construct a fallback painter if NULL).
	void enablePainting(QPainter* painter)
	{
		invariant();
		Q_ASSERT_X(NULL == this->painter, Q_FUNC_INFO, "Painting is already enabled");
		
		// If no painter specified, create a default one painting to the glWidget.
		if(painter == NULL)
		{
			this->painter = new QPainter(glWidget);
			usingGLWidgetPainter = true;
			StelPainter::setQPainter(this->painter);
			return;
		}
		this->painter = painter;
		StelPainter::setQPainter(this->painter);
		invariant();
	}

	//! Initialize the frame buffer objects.
	void initFBOs()
	{
		invariant();
		Q_ASSERT_X(useFBO(), Q_FUNC_INFO, "We're not using FBO");
		if (NULL == backBuffer)
		{
			Q_ASSERT_X(NULL == frontBuffer, Q_FUNC_INFO, 
			           "frontBuffer is not null even though backBuffer is");

			// If non-power-of-two textures are supported,
			// FBOs must have power of two size large enough to fit the viewport.
			const QSize bufferSize = nonPowerOfTwoTexturesSupported 
			    ? StelUtils::smallestPowerOfTwoSizeGreaterOrEqualTo(viewportSize) 
			    : viewportSize;

			// We want a depth and stencil buffer attached to our FBO if possible.
			const QGLFramebufferObject::Attachment attachment =
				QGLFramebufferObject::CombinedDepthStencil;
			backBuffer  = new QGLFramebufferObject(bufferSize, attachment);
			frontBuffer = new QGLFramebufferObject(bufferSize, attachment);

			Q_ASSERT_X(backBuffer->isValid() && frontBuffer->isValid(),
			           Q_FUNC_INFO, "Framebuffer objects failed to initialize");
		}
		invariant();
	}
	
	//! Swap front and back buffers, when using FBO.
	void swapFBOs()
	{
		invariant();
		Q_ASSERT_X(useFBO(), Q_FUNC_INFO, "We're not using FBO");
		QGLFramebufferObject* tmp = backBuffer;
		backBuffer = frontBuffer;
		frontBuffer = tmp;
		invariant();
	}

	//! Destroy FBOs, if used.
	void destroyFBOs()
	{
		invariant();
		// Destroy framebuffers
		if(NULL != frontBuffer)
		{
			delete frontBuffer;
			frontBuffer = NULL;
		}
		if(NULL != backBuffer)
		{
			delete backBuffer;
			backBuffer = NULL;
		}
		invariant();
	}
};

#endif // _STELQGLVIEWPORT_HPP_
