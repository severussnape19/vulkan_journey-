#![allow(unused)]

use core::ffi::c_str;
use std::ffi::{CStr, c_char};

use ash_window::enumerate_required_extensions;
use raw_window_handle::{HasDisplayHandle, HasWindowHandle};
use winit::{application::ApplicationHandler, event::WindowEvent, event_loop::EventLoop, window::Window};
use ash::{Device, EntryFnV1_3, Instance, khr::{self, surface, swapchain}, prelude::VkResult, vk::{self, ApplicationInfo, ImageUsageFlags, PFN_vkGetDeviceQueue, PhysicalDevice, StructureType, SwapchainCreateInfoKHR}};
use std::rc::Rc;

#[allow(non_snake_case)]
fn LOG(msg: &str) { println!("[LOG] {msg}"); }

#[derive(Default)]
struct App {
    window: Option<Rc<Window>>,
    vulk_app: Option<VulkanApp>,
}

struct VulkanApp {
    entry: ash::Entry,
    instance: ash::Instance,
    physical_device: vk::PhysicalDevice,
    queue_family_index: u32,
    device: ash::Device,
    queue: vk::Queue
}

impl VulkanApp {
    fn new(window: &winit::window::Window) -> Result<Self, Box<dyn std::error::Error>> {
        let entry = unsafe { ash::Entry::load().unwrap() };
        let instance = Self::create_instance(&entry, window)?;
        LOG("Instance created!");

        let surface_loader = ash::khr::surface::Instance::new(&entry, &instance);
        let surface_khr = unsafe {
            ash_window::create_surface(
                &entry,
                &instance,
                window.display_handle().unwrap().as_raw(),
                window.window_handle().unwrap().as_raw(),
                None
            ).unwrap()
        };

        let (physical_device, queue_family_index) =
            Self::pick_physical_device(&instance, &surface_loader, &surface_khr);
        LOG("Queue family acquired!");

        let (device, queue) = Self::create_logical_device(&instance, &physical_device, queue_family_index);
        LOG("Created logical device and queue!");

        Ok( Self {
            entry,
            instance,
            physical_device,
            queue_family_index,
            device,
            queue,
        })
    }

    fn create_instance(entry: &ash::Entry, window: &Window) -> VkResult<Instance> {

        let app_info = vk::ApplicationInfo::default()
            .application_name(c"Vulkan ash")
            .application_version(vk::make_api_version(0, 0,  1, 0))
            .api_version(vk::API_VERSION_1_3);

        let layers: Vec<*const c_char> = vec![c"VK_LAYER_KHRONOS_validation".as_ptr()];

        let display_handle = window.display_handle().unwrap_or_else(|err| {
            panic!("Could not acquire display handle!");
        }).as_raw();

        let extensions = enumerate_required_extensions(display_handle).unwrap().to_vec();

        let extension_properties = unsafe {
            entry.enumerate_instance_extension_properties(None).unwrap_or_else(|err| {
                eprintln!("[ERR] {err}");
                panic!("Cannot initialize vulkan without having all required extensions!");
            })
        };

        let mut avl_extensions: Vec<&std::ffi::CStr> = vec![];
        for e in extension_properties {
            let raw_name_ptr = e.extension_name.as_ptr();
            let ext_name = unsafe {
                std::ffi::CStr::from_ptr(raw_name_ptr)
            };
            avl_extensions.push(ext_name);
        }

        for &req_ext in &extensions {
            let req = unsafe { std::ffi::CStr::from_ptr(req_ext) };
            let is_supported = avl_extensions.iter().find(|avl_ext| **avl_ext == req);

            if is_supported.is_none() {
                eprintln!("[ERR] Required extension is not supported by the GPU driver!: {:?}", req);
            }
        }

        let create_info = vk::InstanceCreateInfo::default()
            .application_info(&app_info)
            .enabled_layer_names(&layers)
            .enabled_extension_names(&extensions);

        unsafe { entry.create_instance(&create_info, None) }
    }

    fn pick_physical_device(
        instance: &Instance,
        surface_loader: &ash::khr::surface::Instance,
        surface: &vk::SurfaceKHR,
    ) -> (vk::PhysicalDevice, u32) {

        let avl_physical_devices = unsafe { instance.enumerate_physical_devices().unwrap() };
        if avl_physical_devices.is_empty() {
            panic!("COULD NOT FIND ANY GPU WITH VULKAN SUPPORT!")
        }

        for avl_dev in &avl_physical_devices {
            if let Some(queue_idx) = Self::is_device_suitable(instance, avl_dev, surface_loader, surface) {
                return (*avl_dev, queue_idx);
            }
        }

        panic!("[FATAL ERR] Could not find a suitable vulkan device!")
    }

    fn is_device_suitable(
       instance: &Instance,
        device: &vk::PhysicalDevice,
        surface_loader: &ash::khr::surface::Instance,
        surface: &vk::SurfaceKHR,
    ) -> Option<u32> {
        /* Check:
            * required device properties and features
            * if the required extensions are supported by the device
            * if the device has required queue families
            * if the device supports presentation

          Returns: queue family index
        */
        let device_props = unsafe { instance.get_physical_device_properties(*device) };
        let device_feats = unsafe { instance.get_physical_device_features(*device) };

        let required_properties: bool =
            device_props.device_type == vk::PhysicalDeviceType::DISCRETE_GPU;

        //let required_features: bool = device_feats.geometry_shader ==  vk::TRUE;

        let req_device_extensions = [ash::khr::swapchain::NAME];
        let avl_device_extensions: Vec<vk::ExtensionProperties> = unsafe {
            instance.enumerate_device_extension_properties(*device).unwrap()
        };

        let required_extensions_available: bool = req_device_extensions.iter().all(|req_ext| {
            avl_device_extensions.iter().any(|avl_ext| {
                let avl = unsafe { std::ffi::CStr::from_ptr(avl_ext.extension_name.as_ptr()) };
                avl == req_ext
            })
        });

        let queue_props: Vec<vk::QueueFamilyProperties> = unsafe {
            instance.get_physical_device_queue_family_properties(*device)
        };

        let mut queue_index: Option<u32> = None;
        // GRAPHICS BIT and COMPUTE BIT
        let target_flags: vk::QueueFlags = vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE;

        #[allow(clippy::needless_range_loop)]
        for i in 0..queue_props.len() {
            let contains_flags = queue_props[i].queue_flags.contains(target_flags);
            let presentation_support = unsafe {
                surface_loader.get_physical_device_surface_support(
                    *device,
                    i as u32,
                    *surface
                ).unwrap_or(false)
            };

            if contains_flags && presentation_support {
                queue_index = Some(i as u32);
                break;
            }
        }
        if required_properties && required_extensions_available {
            queue_index
        } else {
            None
        }
    }

    fn create_logical_device(
        instance: &Instance,
        physical_device: &PhysicalDevice,
        queue_family_index: u32
    ) -> (ash::Device, vk::Queue) {
        let priority: [f32; 1] = [1.0f32];
        let queue_create_info  = vk::DeviceQueueCreateInfo::default()
            .queue_family_index(queue_family_index)
            .queue_priorities(&priority);

        let queue_create_infos: [vk::DeviceQueueCreateInfo; 1] = [queue_create_info];
        let enabled_extensions = [ash::khr::swapchain::NAME.as_ptr()];

        let device_create_info = vk::DeviceCreateInfo::default()
            .queue_create_infos(&queue_create_infos)
            .enabled_extension_names(&enabled_extensions);

        let device = unsafe { instance.create_device(
            *physical_device,
            &device_create_info,
            None
            ).unwrap()
        };

        let queue = unsafe {
            device.get_device_queue(queue_family_index, 0)
        };
        (device, queue)
    }

    fn create_swap_chain(instance: &Instance, surface: &vk::SurfaceKHR, ) {
        let image_usage_flags = ash::vk::ImageUsageFlags::TRANSFER_SRC;
        let swapchain_create_info = ash::vk::SwapchainCreateInfoKHR::default();
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &winit::event_loop::ActiveEventLoop) {
        if self.window.is_none() {
            let window_attr = Window::default_attributes()
                .with_title("Vulkan ash")
                .with_inner_size(winit::dpi::LogicalSize::new(1280, 720));

            let window = Rc::new(event_loop.create_window(window_attr).unwrap());
            self.window = Some(window.clone());

            match VulkanApp::new(&window) {
                Ok(vulk_istance) => {
                    self.vulk_app = Some(vulk_istance);
                    println!("Vulkan successfully initialized !");
                }
                Err(e) => {
                    eprintln!("Error creating a window: {}", e);
                    event_loop.exit();
                },
            };
        }
    }

    fn window_event(
        &mut self,
        event_loop: &winit::event_loop::ActiveEventLoop,
        window_id: winit::window::WindowId,
        event: WindowEvent,
    ) {
        let Some(window) = &self.window else { return };
        if window.id() == window_id {
            match event {
                WindowEvent::CloseRequested => {
                    println!("Window close requested!");
                    event_loop.exit();
                }
                WindowEvent::RedrawRequested => {
                    if let Some(_vk) = &self.vulk_app {

                    }
                }
                _ => {}
            }
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(winit::event_loop::ControlFlow::Poll);

    let mut app = App::default();
    event_loop.run_app(&mut app)?;

    Ok(())
}
