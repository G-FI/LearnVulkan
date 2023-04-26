# 软件
1. .editotrconfig
```
	root = true  # 所在目录是项目根目录，此目录及子目录下保存的文件都会生效    
 
	[*]  # 对于所有文件
	indent_style = tab  # 缩进风格
	tab_width = 4  # 缩进宽度
	charset = utf-8-bom  # 文件编码格式，Visual Studio使用utf-8-bom，以防警告
	end_of_line = crlf  # 行尾格式，Windows一般为CRLF，Linux一般为LF，根据需要更改
	insert_final_newline = true   #文件结尾添加换行符，以防警告

```
2. 
# 记录
1. Vulkan中的资源管理可以使用<memory>中提供的自定义内存分配和释放函数来使用只能指针进行操作
2. Vulkan API的设计是紧紧围绕最小化驱动程序开销进行的
3. Vulkan中的实例、设备、扩展一般都是有属性的，查看属性的方法也比较固定1.vkGetXXXXproperties来获取数量，2.创建属性vector 3. vkGetXXXproperties获取属性
4. Vulkan没有默认帧缓冲的概念，它需要一个能够缓冲渲染操作的组件
5. Vk中许多选项需要自己设置，**需要写大量的辅助函数**，目的就是**运行时动态检查配置**，从而选择效果最好，或者速度最快的配置
6. shader的保存选项不能是UTF-8要不然，glslc /glslangValidator报错
6. CPU很快地速度提交指令，但却没有在下一次指令提交时检查上一次提交的指令是否已经执行结束。也就是CPU提交指令快过GPU对指令的处理速度，造GPU需要处理的指令大量堆积， 并且对多个帧同时使用了相同的信号量，它会出什么问题呢？
6. 顶点数据的绑定和顶点属性是在创建渲染管线时指定的
6. Staging Buffer 
    1. VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT的内存 GPU访问更快，但是CPU却访问不到这块设备内存
	2. 
10. 创建纹理时，使用stb_load之后，由于使用staging buffer，所以需要先将读取到的纹理数据memcpy到stagingMemory地区，然后再使用copyBufferToImage
11. 之前图像一直闪，原因：由于swapchain中有3个image，而此时的MAX_FRAME_IN_FIGHT=2，在渲染循环中会根据updateUniformBuffer(currentFrame)根据currentFrame只会更新前两个UBO，而vkAcquireNextImageKHR从swapchain中拿到的可能是三个图像中的任意一个索引，所以选择对应的commandBuffer[imageIdx]然后将其丢给submitinfo，vkQueueCommit(),当imageIdx为2时，使用commandBuffer[2], 而它绑定的Descriptor set没有值的，所以画出是黑的，因此出现一闪一闪情况
# TODOS
1. 什么是子流程，为什么会有子流程依赖
1. 坐标系的问题:  https://zhuanlan.zhihu.com/p/339295068
1. 将vertexBuffer和indexBuffer合并为一个buffer, 在vkCmdXXX阶段使用offset分别指定两个数据的位置，可以增加局部性，就像我OpenGL中做的一样使用，顶点，索引，法线使用同一个VBO
4. 究竟怎样理解layout，binding，uniform
4. Descriptor pool开始
