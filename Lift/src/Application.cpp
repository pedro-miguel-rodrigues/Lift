#include "pch.h"
#include "Application.h"

#include "Log.h"

#include "ImGui/ImguiLayer.h"

#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/Optix/OptixErrorCodes.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Events/MouseEvent.h"
#include "Input.h"

lift::Application* lift::Application::instance_ = nullptr;

lift::Application::Application() {
	LF_CORE_ASSERT(!instance_, "Application already exists");
	instance_ = this;
	window_ = std::unique_ptr<Window>(Window::Create({"Lift Engine", 1280, 720, 0, 28}));
	window_->SetEventCallback(LF_BIND_EVENT_FN(Application::OnEvent));

	InitGraphicsContext();
	InitOptix();

	//window_->SetVSync(false);
	PushOverlay<ImGuiLayer>();
}

lift::Application::~Application() {
	RenderCommand::Shutdown();
}

void lift::Application::Run() {
	SetOptixVariables();
	CreateRenderFrame();
	CreateScene();

	while (is_running_) {
		ImGuiLayer::Begin();
		RenderCommand::Clear();

		UpdateOptixVariables();

		// Render
		optix_context_->launch(0, window_->GetWidth(), window_->GetHeight());
		hdr_texture_->Bind();
		pixel_output_buffer_->Bind();
		// Display
		output_shader_->Bind();
		output_shader_->SetTexImage2D(window_->GetWidth(), window_->GetHeight());
		Renderer::Submit(vertex_array_);

		// Update Layers
		for (auto& layer : layer_stack_)
			layer->OnUpdate();

		for (auto& layer : layer_stack_)
			layer->OnImguiRender();
		EndFrame();
	}
}

void lift::Application::InitOptix() {
	optix_context_ = optix::Context::create();

	GetOptixSystemInformation();

	pixel_output_buffer_ = std::make_unique<PixelBuffer>(
		static_cast<float>(window_->GetWidth()) * window_->GetHeight() * sizeof(float) * 4);
	hdr_texture_ = std::make_unique<Texture>();

	ptx_programs_["ray_generation"] = optix_context_->createProgramFromPTXString(
		Util::GetPtxString("res/ptx/ray_generation.ptx"), "ray_generation");
	ptx_programs_["exception"] = optix_context_->createProgramFromPTXString(
		Util::GetPtxString("res/ptx/exception.ptx"), "exception");
	ptx_programs_["miss"] = optix_context_->createProgramFromPTXString(
		Util::GetPtxString("res/ptx/miss.ptx"), "miss_gradient");
	ptx_programs_["triangle_bounding_box"] = optix_context_->createProgramFromPTXString(
		Util::GetPtxString("res/ptx/triangle_bounding_box.ptx"), "triangle_bounding_box");
	ptx_programs_["triangle_intersection"] = optix_context_->createProgramFromPTXString(
		Util::GetPtxString("res/ptx/triangle_intersection.ptx"), "triangle_intersection");
	ptx_programs_["closest_hit"] = optix_context_->createProgramFromPTXString(
		Util::GetPtxString("res/ptx/closest_hit.ptx"), "closest_hit");

	optix_context_->setRayTypeCount(1);
	optix_context_->setEntryPointCount(1);
	optix_context_->setStackSize(1024);
	optix_context_->setMaxTraceDepth(2);
	optix_context_->setPrintEnabled(true);
	optix_context_->setExceptionEnabled(RT_EXCEPTION_ALL, true);

	buffer_output_ = optix_context_->createBufferFromGLBO(RT_BUFFER_OUTPUT, pixel_output_buffer_->id);
	buffer_output_->setFormat(RT_FORMAT_FLOAT4); //RGBA32F
	buffer_output_->setSize(window_->GetWidth(), window_->GetHeight());
}

void lift::Application::InitGraphicsContext() {
	graphics_context_ = std::make_unique<OpenGLContext>(static_cast<GLFWwindow*>(window_->GetNativeWindow()));
	graphics_context_->Init();
	RenderCommand::SetClearColor({1.0f, 0.1f, 1.0f, 0.0f});
}

void lift::Application::SetOptixVariables() {
	optix_context_->setRayGenerationProgram(0, ptx_programs_["ray_generation"]);
	optix_context_->setExceptionProgram(0, ptx_programs_["exception"]);
	optix_context_->setMissProgram(0, ptx_programs_["miss"]);

	optix_context_["sys_output_buffer"]->set(buffer_output_);
	optix_context_["sys_camera_position"]->setFloat(0.0f, 0.0f, 0.0f);
	optix_context_["sys_camera_u"]->setFloat(1.0f, 0.0f, 0.0f);
	optix_context_["sys_camera_v"]->setFloat(0.0f, 1.0f, 0.0f);
	optix_context_["sys_camera_w"]->setFloat(0.0f, 0.0f, -1.0f);

	optix_context_["sys_color_top"]->set3fv(value_ptr(top_color_));
	optix_context_["sys_color_bottom"]->set3fv(value_ptr(bottom_color_));

}

void lift::Application::UpdateOptixVariables() {
	if (camera_.OnUpdate()) {
		optix_context_["sys_camera_position"]->set3fv(value_ptr(camera_.GetPosition()));
		optix_context_["sys_camera_u"]->set3fv(value_ptr(camera_.GetVectorU()));
		optix_context_["sys_camera_v"]->set3fv(value_ptr(camera_.GetVectorV()));
		optix_context_["sys_camera_w"]->set3fv(value_ptr(camera_.GetVectorW()));
	}
	optix_context_["sys_color_top"]->set3fv(value_ptr(top_color_));
	optix_context_["sys_color_bottom"]->set3fv(value_ptr(bottom_color_));
}

void lift::Application::CreateRenderFrame() {

	float quad_vertices [4 * 5] = {
		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
		1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
		-1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
	};

	vertex_array_.reset(VertexArray::Create());
	std::shared_ptr<VertexBuffer> vertex_buffer{};
	vertex_buffer.reset(VertexBuffer::Create(quad_vertices, sizeof(quad_vertices)));

	vertex_buffer->SetLayout({
		{ShaderDataType::Float3, "a_Position"},
		{ShaderDataType::Float2, "a_Uv"}
	});

	vertex_array_->AddVertexBuffer(vertex_buffer);
	uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
	std::shared_ptr<IndexBuffer> index_buffer;
	index_buffer.reset(IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
	vertex_array_->SetIndexBuffer(index_buffer);

	output_shader_ = std::make_unique<Shader>("res/shaders/texture_quad");
	output_shader_->Bind();
	output_shader_->SetUniform1i("u_Texture", 0);
}

void lift::Application::SetAccelerationProperties(optix::Acceleration plane_acceleration) {
	plane_acceleration->setProperty("vertex_buffer_name", "attributes_buffer");
	plane_acceleration->setProperty("vertex_buffer_stride", "48");
	plane_acceleration->setProperty("indices_buffer_name", "indices_buffer");
	plane_acceleration->setProperty("indices_buffer_stride", "12");
}

void lift::Application::CreateScene() {
	camera_.SetViewport(window_->GetWidth(), window_->GetHeight());
	InitMaterials();
	acceleration_root_ = optix_context_->createAcceleration("Trbvh");
	auto group_root = optix_context_->createGroup();
	group_root->setAcceleration(acceleration_root_);
	group_root->setChildCount(0);

	optix_context_["sys_top_object"]->set(group_root);

	const auto plane_geometry = Util::CreatePlane(1, 1, 1);
	auto plane_geometry_instance = optix_context_->createGeometryInstance();

	plane_geometry_instance->setGeometry(plane_geometry);
	plane_geometry_instance->setMaterialCount(1);
	plane_geometry_instance->setMaterial(0, opaque_material_);

	const auto plane_acceleration = optix_context_->createAcceleration("Trbvh");
	SetAccelerationProperties(plane_acceleration);

	auto plane_geometry_group = optix_context_->createGeometryGroup();
	plane_geometry_group->setAcceleration(plane_acceleration);
	plane_geometry_group->setChildCount(1);
	plane_geometry_group->setChild(0, plane_geometry_instance);

	const auto plane_matrix = scale(mat4(1.0f), {5.0f, 5.0f, 5.0f});

	auto plane_transform = optix_context_->createTransform();
	plane_transform->setChild(plane_geometry_group);
	plane_transform->setMatrix(false, value_ptr(plane_matrix), value_ptr(inverse(plane_matrix)));

	auto count = group_root->getChildCount();
	group_root->setChildCount(count + 1);
	group_root->setChild(count, plane_transform);
	optix_context_["sys_top_object"]->set(group_root);
}

void lift::Application::InitMaterials() {
	opaque_material_ = optix_context_->createMaterial();
	opaque_material_->setClosestHitProgram(0, ptx_programs_["closest_hit"]);
}

void lift::Application::EndFrame() const {
	ImGuiLayer::End();
	graphics_context_->SwapBuffers();
	window_->OnUpdate();
}

void lift::Application::OnEvent(Event& e) {
	for (auto it = layer_stack_.end(); it != layer_stack_.begin();) {
		(*--it)->OnEvent(e);
		if (e.handled_)
			return;
	}

	EventDispatcher dispatcher(e);
	dispatcher.Dispatch<WindowCloseEvent>(LF_BIND_EVENT_FN(Application::OnWindowClose));
	dispatcher.Dispatch<WindowResizeEvent>(LF_BIND_EVENT_FN(Application::OnWindowResize));
	dispatcher.Dispatch<WindowMinimizeEvent>(LF_BIND_EVENT_FN(Application::OnWindowMinimize));
	dispatcher.Dispatch<MouseMovedEvent>(LF_BIND_EVENT_FN(Application::OnMouseMove));
}

bool lift::Application::OnWindowClose(WindowCloseEvent& e) {
	is_running_ = false;
	LF_CORE_TRACE(e.ToString());
	return false;
}

bool lift::Application::OnWindowResize(WindowResizeEvent& e) {
	if (e.GetHeight() && e.GetWidth()) {
		// Only resize when not minimized
		RenderCommand::Resize(e.GetWidth(), e.GetHeight());
		buffer_output_->setSize(e.GetWidth(), e.GetHeight());
		pixel_output_buffer_->Resize(unsigned(buffer_output_->getElementSize()) * e.GetWidth() * e.GetHeight());
		camera_.SetViewport(window_->GetWidth(), window_->GetHeight());
	}
	return false;
}

bool lift::Application::OnWindowMinimize(WindowMinimizeEvent& e) const {
	LF_CORE_ERROR(e.ToString());
	return false;
}

inline bool lift::Application::OnMouseMove(MouseMovedEvent& e) {
	switch (camera_.GetState()) {
	case CameraState::None: {
		if (Input::IsMouseButtonPressed(LF_MOUSE_BUTTON_1))
			camera_.SetState(e.GetX(), e.GetY(), CameraState::Orbit);
		else if (Input::IsMouseButtonPressed(LF_MOUSE_BUTTON_2))
			camera_.SetState(e.GetX(), e.GetY(), CameraState::Dolly);
		else if (Input::IsMouseButtonPressed(LF_MOUSE_BUTTON_3))
			camera_.SetState(e.GetX(), e.GetY(), CameraState::Pan);
		break;
	}
	case CameraState::Orbit: {
		if (!Input::IsMouseButtonPressed(LF_MOUSE_BUTTON_1))
			camera_.SetState(CameraState::None);
		else
			camera_.Orbit(e.GetX(), e.GetY());
		break;
	}
	case CameraState::Dolly: {
		if (!Input::IsMouseButtonPressed(LF_MOUSE_BUTTON_2))
			camera_.SetState(CameraState::None);
		else
			camera_.Dolly(e.GetX(), e.GetY());

		break;
	}
	case CameraState::Pan: {
		if (!Input::IsMouseButtonPressed(LF_MOUSE_BUTTON_3))
			camera_.SetState(CameraState::None);
		else
			camera_.Pan(e.GetX(), e.GetY());

		break;
	}
	default: LF_CORE_ERROR("Invalid Camera State");
	}

	return false;
}


void lift::Application::GetOptixSystemInformation() {
	unsigned int optix_version;
	OPTIX_CALL(rtGetVersion(&optix_version));

	const auto major = optix_version / 10000;
	const auto minor = (optix_version % 10000) / 100;
	const auto micro = optix_version % 100;
	LF_CORE_INFO("");
	LF_CORE_INFO("Optix Info:");
	LF_CORE_INFO("\tVersion: {0}.{1}.{2}", major, minor, micro);

	const auto number_of_devices = optix::Context::getDeviceCount();
	LF_CORE_INFO("\tNumber of Devices = {0}", number_of_devices);

	for (unsigned i = 0; i < number_of_devices; ++i) {
		char name[256];
		optix_context_->getDeviceAttribute(int(i), RT_DEVICE_ATTRIBUTE_NAME, sizeof(name), name);
		LF_CORE_INFO("\tDevice {0}: {1}", i, name);

		int compute_capability[2] = {0, 0};
		optix_context_->getDeviceAttribute(int(i), RT_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY, sizeof(
											   compute_capability), &compute_capability);
		LF_CORE_INFO("\t\tCompute Support: {0}.{1}", compute_capability[0], compute_capability[1]);
	}
}
