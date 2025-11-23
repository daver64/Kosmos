/***
 * Copyright 2004-2025, Dave Rowbotham and Toni Ylisirnio
 * All rights reserved.
 *
 * License: BSD.
 *
 */



#include <sl.h>

SLCamera* current_camera = NULL;

SLCamera* sl_get_current_camera()
{
	return current_camera;
}
SLCamera* sl_create_simple_camera(Vec3d position,double_t fov , double_t near_z , double_t far_z )
{
	SLCamera* c = new SLCamera;
	if (!current_camera)
		current_camera = c;
	c->set_viewport_params(0, 0, screen_w, screen_h);
	c->position =position;
	c->fov = fov;
	c->nearz = near_z;
	c->farz = far_z;
	return c;
}
void sl_update_simple_camera(SLCamera* camera, float_t speed, double_t delta_t_ms)
{
	sl_update_mouselook(camera, speed, delta_t_ms);
	sl_update_keyboardlook(camera, speed, delta_t_ms);
	camera->set_3d_transform();
}
void sl_update_mouselook(SLCamera* camera, float_t speed, double_t delta_t_ms)
{
	static double_t heading{ 0 };
	static double_t pitch{ 0 };
	static double_t yaw{ 0 };
	static double_t bank{ 0 };
	//static bool firstpass = true;
	//if (firstpass)
	//{
	//	firstpass = false;
	//	camera->rquat.get_headings(&heading, &pitch, &bank);
	//}
	sl_show_win_mouse(false);
	int32_t mouse_dx = 0;
	int32_t mouse_dy = 0;
	double_t mpitch = 0;
	double_t myaw = 0;
	double_t mbank = 0;
	sl_get_mouse_mickeys(mouse_dx, mouse_dy);

	mpitch = mouse_dy;
	mpitch /= 500.;
	//if (sl_is_keyheld(VK_LBUTTON))
	{
	//	mbank = mouse_dx;
	//	mbank /= 500.;
	}
	//else
	{
		myaw = mouse_dx;
		myaw /= 500.;
	}


	heading -= myaw;
	pitch += mpitch;
	bank += mbank;
	camera->rquat.set(heading, pitch, bank);

	//get_current_camera()->position = position;
	// keep mouse in center of screen
	// if we have the focus.
	static double mouse_clamp = -0.01;
	HWND active_hwnd = GetForegroundWindow();
	HWND voxworld_hwnd = sl_get_hwnd();
	if (active_hwnd == voxworld_hwnd && mouse_clamp < 0)
	{
		sl_set_mouse_xy(screen_w / 2, screen_h / 2);
		mouse_clamp = 0.1;
	}
	mouse_clamp -= delta_t_ms;
	if (mouse_clamp < 0)
		mouse_clamp = -0.01;
}

void sl_update_keyboardlook(SLCamera* camera, float_t speed, double_t delta_t_ms)
{

	if (sl_is_keyactive(VK_W))
	{
		camera->position = camera->position + (camera->forward_vector() * speed * (delta_t_ms / 1000.0));
	}
	if (sl_is_keyactive(VK_S))
	{
		camera->position = camera->position - (camera->forward_vector() * speed * (delta_t_ms / 1000.0));
	}
	if (sl_is_keyactive(VK_A))
	{
		camera->position = camera->position + (camera->left_vector() * speed * (delta_t_ms / 1000.0));
	}
	if (sl_is_keyactive(VK_D))
	{
		camera->position = camera->position + (camera->right_vector() * speed * (delta_t_ms / 1000.0));
	}

}

SLCamera::SLCamera() 
{
	farz = 1e2;
	nearz = 0.5;
	fov = 60.0;
	parent = nullptr;
	external = false;
	rmat = rquat.to_matrix();
	bboard_mat = rquat.to_matrix();
	for (int32_t n = 0; n < 3; n++) 
	{
		bboard_mat.x[n] = -rmat.x[n];
		bboard_mat.z[n] = -rmat.z[n];
	}
}
void SLCamera::set_viewport_params(int32_t vx, int32_t vy, int32_t vw, int32_t vh) 
{
	viewport_x = vx;
	viewport_y = vy;
	viewport_w = vw;
	viewport_h = vh;
}
void SLCamera::set_projection() 
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(viewport_x, viewport_y, viewport_w, viewport_h);
	gluPerspective(fov, (double_t)viewport_w / (double_t)viewport_h, nearz, farz);
	glMatrixMode(GL_MODELVIEW);
}
bool SLCamera::point_within_frustum(Vec3i p)
{
	Vec3d tp{ (double_t)p.x,(double_t)p.y,(double_t)p.z };
	return point_within_frustum(tp);
}
bool SLCamera::point_within_frustum(Vec3f p)
{
	Vec3d tp{ (double_t)p.x,(double_t)p.y,(double_t)p.z };
	return point_within_frustum(tp);
}
 
bool SLCamera::point_within_frustum(Vec3<>& p) 
{
	EIntersectionRelation3D point_relation = view_frustum[0].classify_point_relation(p);
	if (point_relation == ISREL3D_BACK)
		return false;
	point_relation = view_frustum[1].classify_point_relation(p);
	if (point_relation == ISREL3D_BACK)
		return false;
	point_relation = view_frustum[2].classify_point_relation(p);
	if (point_relation == ISREL3D_BACK)
		return false;
	point_relation = view_frustum[3].classify_point_relation(p);
	if (point_relation == ISREL3D_BACK)
		return false;
	point_relation = view_frustum[4].classify_point_relation(p);
	if (point_relation == ISREL3D_BACK)
		return false;
	point_relation = view_frustum[5].classify_point_relation(p);
	if (point_relation == ISREL3D_BACK)
		return false;

	return true;
}
void SLCamera::set_frustum_vars()
{
	vratio = (double_t)viewport_w / (double_t)viewport_h;
	hnear = std::tan(sl_degtorad(fov / 2.)) * nearz;
	wnear = hnear * vratio;
	hfar = std::tan(sl_degtorad(fov / 2.)) * farz;
	wfar = hfar * vratio;
}
void SLCamera::get_frustum_points_near(Vec3<>& near_center, Vec3<>& near_top_left,
	Vec3<>& near_top_right, Vec3<>& near_bottom_left, Vec3<>& near_bottom_right)
{
	Vec3<> up = rquat.to_matrix().y;
	Vec3<> right = -Vec3<>(rquat.to_matrix().x);
	near_center = position + forward_vector();

	near_top_left = near_center + (up * hnear) - (right * wnear);
	near_top_right = near_center + (up * hnear) + (right * wnear);
	near_bottom_left = near_center - (up * hnear) - (right * wnear);
	near_bottom_right = near_center - (up * hnear) + (right * wnear);
}
Vec3<> SLCamera::left_vector() 
{
	return Vec3<>(rquat.to_matrix().x).normalise();
}
Vec3<> SLCamera::right_vector() 
{
	return -Vec3<>(rquat.to_matrix().x).normalise();
}
Vec3<> SLCamera::up_vector() 
{
	return Vec3<>(rquat.to_matrix().y).normalise();
}
Vec3<> SLCamera::down_vector() 
{
	return -Vec3<>(rquat.to_matrix().y).normalise();
}
Vec3<> SLCamera::forward_vector() 
{
	return Vec3<>(rmat.z[0], rmat.z[1], rmat.z[2]).normalise();
}
Vec3<> SLCamera::back_vector() 
{
	return -Vec3<>(rmat.z[0], rmat.z[1], rmat.z[2]).normalise();
}

void SLCamera::get_frustum_points_far(Vec3<>& far_center, Vec3<>& far_top_left,
	Vec3<>& far_top_right, Vec3<>& far_bottom_left, Vec3<>& far_bottom_right) 
{
	Vec3<> up = rquat.to_matrix().y;
	Vec3<> right = -Vec3<>(rquat.to_matrix().x);
	far_center = position + (forward_vector() * farz);

	far_top_left = far_center + (up * hfar) - (right * wfar);
	far_top_right = far_center + (up * hfar) + (right * wfar);
	far_bottom_left = far_center - (up * hfar) - (right * wfar);
	far_bottom_right = far_center - (up * hfar) + (right * wfar);
}
void SLCamera::set_frustum() 
{
	Vec3d up = rquat.to_matrix().y;
	Vec3d right = -Vec3d(rquat.to_matrix().x);
	// slightly wide
	up = up * 1.1;
	right = right * 1.1;
	Vec3d fc = position + (forward_vector() * farz);
	Vec3d ftl = fc + (up * hfar) - (right * wfar);
	Vec3d ftr = fc + (up * hfar) + (right * wfar);
	Vec3d fbl = fc - (up * hfar) - (right * wfar);
	Vec3d fbr = fc - (up * hfar) + (right * wfar);
	// pull back to behind camera
	Vec3d nc = position + (forward_vector() * -1);
	Vec3d ntl = nc + (up * hnear) - (right * wnear);
	Vec3d ntr = nc + (up * hnear) + (right * wnear);
	Vec3d nbl = nc - (up * hnear) - (right * wnear);
	Vec3d nbr = nc - (up * hnear) + (right * wnear);
	// 0=nearz, 1=farz, 2=left, 3=right, 4=top , 5=bottom
	view_frustum[0].set_plane(ntl, ntr, nbr);
	view_frustum[1].set_plane(ftr, ftl, fbl);
	view_frustum[2].set_plane(ntl, nbl, fbl);
	view_frustum[3].set_plane(nbr, ntr, fbr);
	view_frustum[4].set_plane(ntr, ntl, ftl);
	view_frustum[5].set_plane(nbl, nbr, fbr);
}
void SLCamera::get_glu_lookat_params(Vec3d& eye, Vec3d& center, Vec3d& up)
{
	eye.x = position.x;
	eye.y = position.y;
	eye.z = position.z + 0.001f;
	center.x = position.x + rmat.z[0];
	center.y = position.y + rmat.z[1];
	center.z = position.z + rmat.z[2];
	up.x = rmat.y[0];
	up.y = rmat.y[1];
	up.z = rmat.y[2];
}
void SLCamera::set_3d_transform() 
{
	set_projection();
	glLoadIdentity();

	rmat = rquat.to_matrix();
	set_frustum_vars();
	set_frustum();
	gluLookAt(position.x, position.y, position.z + 0.001,
		position.x + rmat.z[0], position.y + rmat.z[1], position.z + rmat.z[2],
		rmat.y[0], rmat.y[1], rmat.y[2]);
}
void SLCamera::set_doom_3d_transform()
{
	set_projection();
	glLoadIdentity();

	rmat = rquat.to_matrix();
	set_frustum_vars();
	set_frustum();
	rmat.y[0] = 0;
	rmat.y[1] = 1;
	rmat.y[0] = 0;
	gluLookAt(position.x, position.y, position.z + 0.001,
		position.x + rmat.z[0], position.y + rmat.z[1], position.z + rmat.z[2],
		rmat.y[0], rmat.y[1], rmat.y[2]);
}
// Returns distance to object
double_t SpaceSLCamera::set_view(SLObject3d* obj) 
{
	Vec3<> offset = sl_relative_to_relative(parent, obj).transform(Vec3<>(0, 0, 0));
	double_t dist = offset.length();
	farz = (double_t)((dist + obj->radius * 2) * 1.42);
	if (std::log(farz) > 5)
		nearz = (double_t)((std::max)(10.0, dist - obj->radius * 2) * .706);
	else nearz = (double_t)((std::max)(1.0, dist - obj->radius * 2) * .706);
	set_3d_transform();
	apply_translation(obj);
	return dist;
}
void SpaceSLCamera::apply_translation(SLObject3d* obj) 
{
	Matrix<> tform = sl_relative_to_relative(parent, obj);
	tform.apply();
}
void SLCamera::apply_translation(Vec3<>& offset) 
{ 
	glTranslated(offset.x, offset.y, offset.z); 
}
double_t SLCamera::get_screen_size(SLObject3d* obj, double_t distance)
{
	double_t SCREEN_H = 1.0;//FIXME!!!
	double_t ppd = ((double_t)SCREEN_H) / fov;  // Pixels per degree
	double_t alpha = sl_radtodeg(std::atan2(obj->radius, distance));
	return (alpha * ppd) * 2;
}
void SLCamera::billboard(Vec3d& pos, double_t size) 
{
	glPushMatrix();
	glTranslated(pos.x, pos.y, pos.z);
	bboard_mat.apply();
	glBegin(GL_QUADS);
	size = size / 2;
	glNormal3d(0, 0, 1);
	glTexCoord2d(0, 1); 
	glVertex3d(-size, size, 0);
	glTexCoord2d(0, 0); 
	glVertex3d(-size, -size, 0);
	glTexCoord2d(1, 0); 
	glVertex3d(size, -size, 0);
	glTexCoord2d(1, 1); 
	glVertex3d(size, size, 0);
	glEnd();
	glPopMatrix();
}
void SLCamera::billboard_fixed(Vec3d& pos, double_t abs_size) 
{
	double_t SCREEN_H = 1;//FIXME!
	double_t ppd = ((double_t)SCREEN_H) / fov;
	double_t dist = pos.length();
	double_t alpha = (abs_size / 2.0) * dist;
	double_t size = tan(alpha) / ppd;

	glPushMatrix();
	glTranslated(pos.x, pos.y, pos.z);
	bboard_mat.apply();
	glBegin(GL_QUADS);
	glNormal3d(0, 0, 1);
	glTexCoord2d(0, 1); 
	glVertex3d(-size, size, 0);
	glTexCoord2d(0, 0); 
	glVertex3d(-size, -size, 0);
	glTexCoord2d(1, 0); 
	glVertex3d(size, -size, 0);
	glTexCoord2d(1, 1); 
	glVertex3d(size, size, 0);
	glEnd();
	glPopMatrix();
}
void SLCamera::billboard_quad(Vec3d posd, double_t size) 
{
	Vec3f pos = sl_vec3d_to_vec3f(posd);
	size = size / 2;
	Vec3f x = sl_vec3d_to_vec3f(Vec3d(bboard_mat.x) * size);
	Vec3f y = sl_vec3d_to_vec3f(Vec3d(bboard_mat.y) * size);
	Vec3f temp;
	SLPrimitiveBuffer* p = sl_get_immediate();
	p->begin(GL_QUADS);
	p->normal((float_t)rmat.z[0], (float_t)rmat.z[1], (float_t)rmat.z[2]);
	temp = pos - x + y;
	p->texcoord0(0, 1);
	p->vertex(temp.x, temp.y, temp.z);
	temp = pos - x - y;
	p->texcoord0(0, 0);
	p->vertex(temp.x, temp.y, temp.z);
	temp = pos + x - y;
	p->texcoord0(1, 0);
	p->vertex(temp.x, temp.y, temp.z);
	temp = pos + x + y;
	p->texcoord0(1, 1);
	p->vertex(temp.x, temp.y, temp.z);
	p->end();
}
void SLCamera::billboard_update() 
{
	rmat = rquat.to_matrix();
	bboard_mat = Matrix<>(rmat);
	for (int n = 0; n < 3; n++) 
	{
		bboard_mat.x[n] = -rmat.x[n];
		bboard_mat.z[n] = -rmat.z[n];
	}
}







double_t SpaceSLCamera::get_distance(SLObject3d* obj) 
{
	return sl_relative_to_relative(parent, obj).transform(Vec3d(0, 0, 0)).length();
}

DimetricSLCamera::DimetricSLCamera() :SLCamera() 
{
	minzoom = 1.f; maxzoom = 5.0f; zoom = 1.0f;
}
void DimetricSLCamera::ZoomIn(const double_t& step) 
{
	zoom += (float_t)step;
	if (zoom >= maxzoom)
		zoom = maxzoom;
}
void DimetricSLCamera::ZoomOut(const double_t& step) 
{
	zoom -= (float_t)step;
	if (zoom <= minzoom)
		zoom = minzoom;
}
void DimetricSLCamera::set_projection() 
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(viewport_x, viewport_y, viewport_w, viewport_h);
	glOrtho(-((GLdouble)viewport_w / (GLdouble)viewport_h) * zoom,
		((GLdouble)viewport_w / (GLdouble)viewport_h) * zoom,
		-1.f * zoom, 1.f * zoom, nearz, farz);
	glMatrixMode(GL_MODELVIEW);
}

FlightSLCamera::FlightSLCamera() 
{
	position = Vec3d(0, 0, 0);
	farz = 5e3;
	nearz = 2;
	parent = nullptr;
	external = false;
}
void FlightSLCamera::roll(double_t speed) 
{ 
	rquat.roll(speed); 
}
void FlightSLCamera::pitch(double_t speed) 
{ 
	rquat.pitch(speed); 
}
void FlightSLCamera::yaw(double_t speed) 
{ 
	rquat.yaw(speed); 
}

ExternalSLCamera::ExternalSLCamera(SLObject3d* t) 
{
	target = t;
	rad = 20;
	external = true;
	parent = target;
}

void ExternalSLCamera::set_3d_transform() 
{
	set_projection();
	glLoadIdentity();
	rmat = rquat.to_matrix();
	set_frustum_vars();

	gluLookAt(rmat.z[0] * rad, rmat.z[1] * rad, rmat.z[2] * rad,
		0.0, 0.0, 0.0,
		rmat.y[0], 1.0, rmat.y[2]);
}



SLCameraControl::SLCameraControl()
{
	font = sl_create_winfont("Consolas", 12, FW_NORMAL);
	cam = new SLCamera();
	cam->fov = 50;
	cam->nearz = 0.1;
	cam->farz = 100;
	cam->set_viewport_params(0, 0, screen_w, screen_h);
	cam->position = Vec3d(0, 0, -2);
	speed = 10;
}
SLCameraControl::~SLCameraControl()
{
	delete cam;
}
bool SLCameraControl::point_within_frustum(Vec3d& p)
{
	return cam->point_within_frustum(p);
}
void SLCameraControl::update(double_t delta_t_ms)
{

	if (sl_is_keyactive(VK_UP) && enable_pitch)
	{
		cam->pitch(0.05);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}
	if (sl_is_keyactive(VK_DOWN) && enable_pitch)
	{
		cam->pitch(-0.05);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}
	if (sl_is_keyactive(VK_LEFT) && enable_yaw)
	{
		cam->yaw(0.05);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}
	if (sl_is_keyactive(VK_RIGHT) && enable_yaw)
	{
		cam->yaw(-0.05);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}
	if (sl_is_keyactive(VK_Q) && enable_roll)
	{
		cam->roll(-0.05);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}
	if (sl_is_keyactive(VK_E) && enable_roll)
	{
		cam->roll(0.05);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}
	if (sl_is_keyactive(VK_A) && enable_strafe)
	{
		//cam->roll(-0.05);
		cam->position = cam->position + (cam->left_vector() * (delta_t_ms / 1000.0) * speed);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}
	if (sl_is_keyactive(VK_D) && enable_strafe)
	{

		//cam->roll(0.05);
		cam->position = cam->position + (cam->right_vector() * (delta_t_ms / 1000.0) * speed);
		cam->viewchange_count++;
		//cam->viewchanged = true;
	}


	if (sl_is_keyactive(VK_W) && enable_fb)
	{
		//float speed = 10;

		cam->position = cam->position + (cam->forward_vector() * ((double_t)delta_t_ms / 1000.0) * speed);
		cam->viewchange_count++;
		//cam->viewchanged = true;

	}
	if (sl_is_keyactive(VK_S) && enable_fb)
	{
		//float speed = 10;
		cam->position = cam->position + (cam->back_vector() * ((double_t)delta_t_ms / 1000.0) * speed);
		cam->viewchange_count++;
		//cam->viewchanged = true;

	}
	if (cam->viewchange_count > cam->viewchange_threshold)
	{
		cam->viewchange_count = 0;
		cam->viewchanged = true;
	}
	//cam->Set3dTransform();
	//sl_enable_depthtest();
}
void SLCameraControl::hud(double_t delta_t_ms)
{
	sl_push_matrix();
	sl_disable_texturing();
	sl_draw_axis(32.0);
	sl_disable_depthtest();

	sl_ortho(screen_w, screen_h, FALSE);
	sl_print_matrix(4, 20, x11colours::yellow, cam->rmat,"SLCamera Matrix:");
	sl_print_vector( 4, 120, x11colours::yellow, cam->position,"SLCamera Position:");
	sl_print_vector( 4, 160, x11colours::yellow, cam->forward_vector(),"SLCamera Direction:");

	sl_enable_depthtest();
	sl_enable_texturing();
	sl_pop_matrix();
	//sl_display_frame_data();
}

void SLCameraControl::set_3d_transform()
{
	cam->set_3d_transform();
}

void SLCameraControl::set_doom_3d_transform()
{
	cam->set_doom_3d_transform();
}