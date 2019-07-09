#pragma once

#include "Window.h"

#include <optix_world.h>
#include "LayerStack.h"
#include "Events/ApplicationEvent.h"
#include "Renderer/Shader.h"
#include "Renderer/VertexArray.h"
#include "Renderer/Texture.h"
#include "Platform/OpenGL/PixelBuffer.h"


namespace lift {

	class Application {
	public:
		Application();
		virtual ~Application();

		void Run();

		template <typename T>
		void PushLayer() {
			layer_stack_.PushLayer<T>();
		}

		template <typename T>
		void PushOverlay() {
			layer_stack_.PushOverlay<T>();
		}

		Window& GetWindow() const { return *window_; }
		static Application& Get() { return *instance_; }

		void SetTopColor(vec3 color) {
			top_color_.x = color.x;
			top_color_.y = color.y;
			top_color_.z = color.z;
		}

		void SetBottomColor(vec3 color) {
			bottom_color_.x = color.x;
			bottom_color_.y = color.y;
			bottom_color_.z = color.z;
		}

	private:
		bool is_running_ = true;
		std::unique_ptr<Window> window_;
		std::unique_ptr<GraphicsContext> graphics_context_;
		std::unique_ptr<PixelBuffer> pixel_output_buffer_;	
		std::unique_ptr<Texture> hdr_texture_;

		std::map<std::string, optix::Program> ptx_programs_;
		
		optix::Context optix_context_;
		LayerStack layer_stack_;

		// Temp
		std::shared_ptr<VertexArray> vertex_array_;
		std::shared_ptr<Shader> output_shader_;
		optix::Buffer buffer_output_;
		optix::float3 top_color_, bottom_color_;
		// 

		static Application* instance_;

		void InitOptix();
		void InitGraphicsContext();

		void SetOptixVariables();
		void UpdateOptixVariables();

		void CreateRenderFrame();
		void CreateScene();
		void EndFrame() const;
		void OnEvent(Event& e);
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
		bool OnWindowMinimize(WindowMinimizeEvent& e);

		void GetOptixSystemInformation();
	};

	// Defined by Sandbox
	std::shared_ptr<Application> CreateApplication();
}
